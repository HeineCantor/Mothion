#include "tools/BuiltInBrushes.h"

#include <QObject>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <cstdint>
#include <numbers>

namespace mothion::tools {

namespace {

using brush::BrushStyle;
using brush::Effector;
using brush::EffectorCurve;

// Curve shorthands (control points of PressureCurve), as the other presets use them.
const PressureCurve kLinear;
const PressureCurve kFirm({0.5f, 0.05f}, {0.9f, 0.6f});   // needs pressure to get wide
const PressureCurve kTaper({0.65f, 0.0f}, {0.85f, 0.9f}); // thin hairlines, strong swell
const PressureCurve kSteady({0.0f, 0.75f}, {0.25f, 1.0f}); // nearly constant width
const PressureCurve kSoft({0.2f, 0.4f}, {0.6f, 0.9f});    // light touch, early response

constexpr int kTipSize = 128;

// A blank tip, to paint coverage on in white (only the alpha counts).
QImage blankTip(int size = kTipSize)
{
    QImage tip(size, size, QImage::Format_ARGB32_Premultiplied);
    tip.fill(Qt::transparent);
    return tip;
}

// A round tip with graphite grain: dots of uneven coverage in a disc, denser and darker towards the middle. `seed`
// gives another grain; `fill` is how much of the disc the grain covers.
QImage grainTip(std::uint32_t seed, double fill)
{
    QImage tip = blankTip(64);
    const int n = tip.width();
    std::uint32_t state = seed * 2654435761u + 1u;
    auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return double(state >> 8) / double(1u << 24);
    };
    for (int y = 0; y < n; ++y) {
        auto *line = reinterpret_cast<QRgb *>(tip.scanLine(y));
        for (int x = 0; x < n; ++x) {
            const double dx = (x + 0.5) / n * 2 - 1, dy = (y + 0.5) / n * 2 - 1;
            const double r = std::hypot(dx, dy);
            if (r >= 1.0)
                continue;
            const double edge = std::min(1.0, (1.0 - r) * 4.0); // a soft rim a quarter of the radius wide
            const double grain = next() < fill ? 0.55 + 0.45 * next() : 0.15 * next();
            const int a = int(std::lround(255 * edge * grain));
            line[x] = qPremultiply(qRgba(255, 255, 255, a));
        }
    }
    return tip;
}

QImage shapeTip(const QPainterPath &shape)
{
    QImage tip = blankTip();
    QPainter p(&tip);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(kTipSize / 24.0, kTipSize / 24.0);
    p.fillPath(shape, Qt::white);
    return tip;
}

QImage starTip()
{
    QPainterPath star;
    for (int i = 0; i < 10; ++i) {
        const double angle = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
        const double r = i % 2 ? 4.6 : 11.0;
        const QPointF at(12 + r * std::cos(angle), 12.6 + r * std::sin(angle));
        i == 0 ? star.moveTo(at) : star.lineTo(at);
    }
    star.closeSubpath();
    return shapeTip(star);
}

QImage leafTip()
{
    QPainterPath leaf; // pointing right, so it follows the direction of the stroke
    leaf.moveTo(1.5, 12);
    leaf.cubicTo(7, 3.5, 17, 3.5, 22.5, 12);
    leaf.cubicTo(17, 20.5, 7, 20.5, 1.5, 12);
    leaf.closeSubpath();
    QPainterPath vein; // the middle vein, left out
    vein.addRect(QRectF(3, 11.5, 17, 1));
    return shapeTip(leaf.subtracted(vein));
}

QImage sparkleTip()
{
    QPainterPath sparkle; // four points, curved in
    sparkle.moveTo(12, 0.5);
    sparkle.quadTo(13, 11, 23.5, 12);
    sparkle.quadTo(13, 13, 12, 23.5);
    sparkle.quadTo(11, 13, 0.5, 12);
    sparkle.quadTo(11, 11, 12, 0.5);
    sparkle.closeSubpath();
    return shapeTip(sparkle);
}

QImage heartTip()
{
    QPainterPath heart;
    heart.moveTo(12, 21);
    heart.cubicTo(3, 14.5, 1, 9, 4.5, 5.5);
    heart.cubicTo(7.5, 2.5, 11, 4, 12, 7);
    heart.cubicTo(13, 4, 16.5, 2.5, 19.5, 5.5);
    heart.cubicTo(23, 9, 21, 14.5, 12, 21);
    heart.closeSubpath();
    return shapeTip(heart);
}

// A random input on a parameter, from `minimum` to its full value.
Effector randomFrom(double minimum, Effector e = {})
{
    e.active |= brush::InputRandom;
    e.randomMin = minimum;
    return e;
}

// The start and end of the stroke thin the parameter down (the in/out input, which is not offered by default).
void taperEnds(Effector &e)
{
    e.available |= brush::InputInOut;
    e.active |= brush::InputInOut;
}

// A shared brush made once by `make` (a static per call site keeps it).
template <class Make>
BrushDefinitionPtr defined(const QString &name, Make make)
{
    auto b = std::make_shared<BrushDefinition>();
    b->name = name;
    make(*b);
    b->style.tipCount = int(b->tips.size());
    return b;
}

void withTips(BrushDefinition &b, QList<QImage> tips)
{
    b.tips = std::move(tips);
}

BrushPreset preset(const char *id, const QString &name, ToolId tool, double size, double opacity,
                   const PressureCurve &curve, double smoothing, BrushDefinitionPtr definition)
{
    BrushPreset p(name, tool, size, opacity, curve, smoothing);
    p.id = QLatin1String(id);
    p.brush = std::move(definition);
    return p;
}

// --- the categories ---

QList<BrushPreset> pencils()
{
    const ToolId t = ToolId::Pencil;
    static const BrushDefinitionPtr hb = defined(QStringLiteral("Pencil HB"), [](BrushDefinition &b) {
        withTips(b, {grainTip(1, 0.55), grainTip(2, 0.55), grainTip(3, 0.55)});
        b.style.tipOrder = brush::TipOrder::Random;
        b.style.rotationMode = brush::RotationOn | brush::RotationRandom;
        b.style.rotationRandom = 1.0;
        b.style.spacing = 0.12;
        b.style.flowEffector = Effector::pressureOnly({}, 0.35);
    });
    static const BrushDefinitionPtr mechanical = defined(QStringLiteral("Mechanical pencil"), [](BrushDefinition &b) {
        withTips(b, {grainTip(4, 0.75), grainTip(5, 0.75)});
        b.style.tipOrder = brush::TipOrder::Random;
        b.style.rotationMode = brush::RotationOn | brush::RotationRandom;
        b.style.rotationRandom = 1.0;
        b.style.spacing = 0.1;
        b.style.sizeEffector = Effector::pressureOnly({}, 0.6); // a lead keeps its width
        b.style.flowEffector = Effector::pressureOnly({}, 0.3);
    });
    static const BrushDefinitionPtr soft = defined(QStringLiteral("Soft pencil 6B"), [](BrushDefinition &b) {
        withTips(b, {grainTip(6, 0.4), grainTip(7, 0.4), grainTip(8, 0.4)});
        b.style.tipOrder = brush::TipOrder::Random;
        b.style.rotationMode = brush::RotationOn | brush::RotationRandom;
        b.style.rotationRandom = 1.0;
        b.style.spacing = 0.1;
        b.style.flow = 0.8;
        b.style.flowEffector = Effector::pressureOnly({}, 0.15);
    });
    static const BrushDefinitionPtr colored = defined(QStringLiteral("Colored pencil"), [](BrushDefinition &b) {
        withTips(b, {grainTip(9, 0.35), grainTip(10, 0.35)});
        b.style.tipOrder = brush::TipOrder::Random;
        b.style.rotationMode = brush::RotationOn | brush::RotationRandom;
        b.style.rotationRandom = 1.0;
        b.style.spacing = 0.15;
        b.style.sizeEffector = Effector::pressureOnly({}, 0.5);
        b.style.flowEffector = Effector::pressureOnly({}, 0.2);
    });
    return {
        preset("pencil/pencil", QObject::tr("Pencil"), t, 6.0, 0.9, kFirm, 0.2, hb),
        preset("pencil/mechanical", QObject::tr("Mechanical pencil"), t, 3.0, 0.85, kLinear, 0.25, mechanical),
        preset("pencil/soft", QObject::tr("Soft pencil"), t, 12.0, 0.9, kSoft, 0.15, soft),
        preset("pencil/colored", QObject::tr("Colored pencil"), t, 9.0, 1.0, kLinear, 0.2, colored),
    };
}

QList<BrushPreset> pens()
{
    const ToolId t = ToolId::Pen;
    static const BrushDefinitionPtr gpen = defined(QStringLiteral("G-pen"), [](BrushDefinition &b) {
        b.style.inOut.in = b.style.inOut.out = true;
        b.style.inOut.inLength = 6.0;
        b.style.inOut.outLength = 10.0;
        taperEnds(b.style.sizeEffector);
    });
    static const BrushDefinitionPtr mapping = defined(QStringLiteral("Mapping pen"), [](BrushDefinition &b) {
        b.style.sizeEffector = Effector::pressureOnly({}, 0.2);
        b.style.atLeastOnePixel = true;
    });
    static const BrushDefinitionPtr marker = defined(QStringLiteral("Marker"), [](BrushDefinition &b) {
        b.style.sizeEffector = Effector::pressureOnly({}, 0.85);
    });
    // The stabilized ones: a heavy stabilizer and a correction after the stroke, for long clean lines.
    static const BrushDefinitionPtr gpenStable = defined(QStringLiteral("G-pen stabilized"), [](BrushDefinition &b) {
        b.style = gpen->style;
        b.style.revision = 4;
        b.style.revisionCurves = true;
    });
    static const BrushDefinitionPtr mappingStable = defined(QStringLiteral("Mapping pen stabilized"), [](BrushDefinition &b) {
        b.style = mapping->style;
        b.style.revision = 3;
        b.style.revisionCurves = true;
    });
    return {
        preset("pen/g-pen", QObject::tr("G-pen"), t, 9.0, 1.0, kTaper, 0.5, gpen),
        preset("pen/g-pen-stabilized", QObject::tr("G-pen stabilized"), t, 9.0, 1.0, kTaper, 0.8, gpenStable),
        preset("pen/pen", QObject::tr("Pen"), t, 5.0, 1.0, kLinear, 0.45, nullptr),
        preset("pen/mapping", QObject::tr("Mapping pen"), t, 3.0, 1.0, kTaper, 0.5, mapping),
        preset("pen/mapping-stabilized", QObject::tr("Mapping pen stabilized"), t, 3.0, 1.0, kTaper, 0.8, mappingStable),
        preset("pen/marker", QObject::tr("Marker"), t, 14.0, 1.0, kSteady, 0.25, marker),
    };
}

QList<BrushPreset> brushes()
{
    const ToolId t = ToolId::Brush;
    static const BrushDefinitionPtr round = defined(QStringLiteral("Round brush"), [](BrushDefinition &b) {
        b.style.hardness = 0.85;
        b.style.flowEffector = Effector::pressureOnly({}, 0.5);
    });
    static const BrushDefinitionPtr ink = defined(QStringLiteral("Ink brush"), [](BrushDefinition &b) {
        b.style.thickness = 0.8;
        b.style.rotationMode = brush::RotationOn | brush::RotationDirection;
        b.style.inOut.in = b.style.inOut.out = true;
        b.style.inOut.inLength = 12.0;
        b.style.inOut.outLength = 30.0;
        taperEnds(b.style.sizeEffector);
    });
    static const BrushDefinitionPtr flat = defined(QStringLiteral("Flat brush"), [](BrushDefinition &b) {
        b.style.hardness = 0.9;
        b.style.thickness = 0.3;
        b.style.rotationMode = brush::RotationOn;
        b.style.rotation = 35.0; // held at an angle, as a flat brush is
        b.style.spacing = 0.04;
        b.style.sizeEffector = Effector::pressureOnly({}, 0.6);
    });
    return {
        preset("brush/round", QObject::tr("Round brush"), t, 32.0, 0.8, kLinear, 0.15, round),
        preset("brush/ink", QObject::tr("Ink brush"), t, 20.0, 1.0, kTaper, 0.35, ink),
        preset("brush/flat", QObject::tr("Flat brush"), t, 36.0, 1.0, kLinear, 0.2, flat),
        preset("brush/fill", QObject::tr("Fill"), t, 80.0, 1.0, kSteady, 0.1, nullptr),
    };
}

QList<BrushPreset> watercolors()
{
    const ToolId t = ToolId::Watercolor;
    static const BrushDefinitionPtr opaque = defined(QStringLiteral("Opaque watercolor"), [](BrushDefinition &b) {
        b.style.hardness = 0.7;
        b.style.mix.on = true;
        b.style.mix.amount = 0.85;
        b.style.mix.density = 0.9;
        b.style.mix.stretch = 0.2;
        b.style.flowEffector = Effector::pressureOnly({}, 0.4);
    });
    static const BrushDefinitionPtr transparent = defined(QStringLiteral("Transparent watercolor"), [](BrushDefinition &b) {
        b.style.hardness = 0.5;
        b.style.mix.on = true;
        b.style.mix.amount = 0.6;
        b.style.mix.density = 0.35;
        b.style.mix.stretch = 0.3;
        b.style.waterEdge.on = true;
        b.style.waterEdge.radius = 4.0;
        b.style.waterEdge.alphaPower = 0.35;
        b.style.waterEdge.valuePower = 0.1;
        b.style.waterEdge.blur = 1.0;
    });
    static const BrushDefinitionPtr wet = defined(QStringLiteral("Wet watercolor"), [](BrushDefinition &b) {
        b.style.hardness = 0.2;
        b.style.mix.on = true;
        b.style.mix.amount = 0.35;
        b.style.mix.density = 0.25;
        b.style.mix.stretch = 0.7;
        b.style.waterEdge.on = true;
        b.style.waterEdge.radius = 6.0;
        b.style.waterEdge.alphaPower = 0.25;
        b.style.waterEdge.blur = 3.0;
        b.style.adjustFlowBySpacing = true;
    });
    return {
        preset("watercolor/opaque", QObject::tr("Opaque watercolor"), t, 40.0, 1.0, kLinear, 0.15, opaque),
        preset("watercolor/transparent", QObject::tr("Transparent watercolor"), t, 50.0, 0.8, kLinear, 0.15, transparent),
        preset("watercolor/wet", QObject::tr("Wet watercolor"), t, 70.0, 0.7, kSoft, 0.1, wet),
    };
}

QList<BrushPreset> airbrushes()
{
    const ToolId t = ToolId::Airbrush;
    static const BrushDefinitionPtr soft = defined(QStringLiteral("Soft airbrush"), [](BrushDefinition &b) {
        b.style.hardness = 0.0;
        b.style.flow = 0.12;
        b.style.flowEffector = Effector::pressureOnly({}, 0.0);
        b.style.sizeEffector = Effector{}; // the pressure gives paint, not width
        b.style.spacing = 0.08;
        b.style.adjustFlowBySpacing = true;
    });
    static const BrushDefinitionPtr hard = defined(QStringLiteral("Hard airbrush"), [](BrushDefinition &b) {
        b.style.hardness = 0.6;
        b.style.flow = 0.2;
        b.style.flowEffector = Effector::pressureOnly({}, 0.0);
        b.style.sizeEffector = Effector{};
        b.style.spacing = 0.08;
        b.style.adjustFlowBySpacing = true;
    });
    static const BrushDefinitionPtr spray = defined(QStringLiteral("Spray"), [](BrushDefinition &b) {
        b.style.spray = true;
        b.style.spraySize = 10.0; // the cloud, as a multiple of a droplet
        b.style.sprayDensity = 6.0;
        b.style.sprayDensityEffector = Effector::pressureOnly({}, 0.2);
        b.style.sizeEffector = randomFrom(0.4);
        b.style.spacing = 0.6;
        b.style.hardness = 0.8;
    });
    return {
        preset("airbrush/soft", QObject::tr("Soft airbrush"), t, 150.0, 1.0, kLinear, 0.0, soft),
        preset("airbrush/hard", QObject::tr("Hard airbrush"), t, 60.0, 1.0, kLinear, 0.0, hard),
        preset("airbrush/spray", QObject::tr("Spray"), t, 3.0, 1.0, kLinear, 0.0, spray),
    };
}

QList<BrushPreset> decorations()
{
    const ToolId t = ToolId::Decoration;
    static const BrushDefinitionPtr dots = defined(QStringLiteral("Scattered dots"), [](BrushDefinition &b) {
        b.style.spray = true;
        b.style.spraySize = 3.0;
        b.style.sprayDensity = 2.0;
        b.style.sizeEffector = randomFrom(0.3);
        b.style.spacing = 1.2;
    });
    static const BrushDefinitionPtr stars = defined(QStringLiteral("Stars"), [](BrushDefinition &b) {
        withTips(b, {starTip()});
        b.style.sizeEffector = randomFrom(0.4);
        b.style.spacing = 1.4;
        b.style.spacingEffector = randomFrom(0.6);
        b.style.rotationMode = brush::RotationOn | brush::RotationRandom;
        b.style.rotationRandom = 0.25;
        b.style.color.hue = 25.0;
        b.style.color.hueEffector = randomFrom(-1.0);
    });
    static const BrushDefinitionPtr leaves = defined(QStringLiteral("Leaves"), [](BrushDefinition &b) {
        withTips(b, {leafTip()});
        b.style.sizeEffector = randomFrom(0.5);
        b.style.spacing = 0.9;
        b.style.rotationMode = brush::RotationOn | brush::RotationDirection | brush::RotationRandom;
        b.style.rotationRandom = 0.3;
        b.style.tipFlip = brush::FlipVAlternate; // on either side of the stroke
        b.style.color.value = 0.25;
        b.style.color.valueEffector = randomFrom(-1.0);
    });
    static const BrushDefinitionPtr sparkles = defined(QStringLiteral("Sparkles"), [](BrushDefinition &b) {
        withTips(b, {sparkleTip()});
        b.style.spray = true;
        b.style.spraySize = 2.5;
        b.style.sprayDensity = 1.0;
        b.style.sizeEffector = randomFrom(0.2);
        b.style.spacing = 1.0;
    });
    static const BrushDefinitionPtr hearts = defined(QStringLiteral("Hearts"), [](BrushDefinition &b) {
        withTips(b, {heartTip()});
        b.style.sizeEffector = randomFrom(0.6);
        b.style.spacing = 1.3;
        b.style.rotationMode = brush::RotationOn | brush::RotationRandom;
        b.style.rotationRandom = 0.15;
    });
    return {
        preset("decoration/dots", QObject::tr("Scattered dots"), t, 8.0, 1.0, kLinear, 0.1, dots),
        preset("decoration/stars", QObject::tr("Stars"), t, 30.0, 1.0, kLinear, 0.2, stars),
        preset("decoration/leaves", QObject::tr("Leaves"), t, 28.0, 1.0, kLinear, 0.3, leaves),
        preset("decoration/sparkles", QObject::tr("Sparkles"), t, 18.0, 1.0, kLinear, 0.1, sparkles),
        preset("decoration/hearts", QObject::tr("Hearts"), t, 26.0, 1.0, kLinear, 0.2, hearts),
    };
}

} // namespace

QList<BrushPreset> brushCategoryPresets(ToolId tool)
{
    switch (tool) {
    case ToolId::Pencil: return pencils();
    case ToolId::Pen: return pens();
    case ToolId::Brush: return brushes();
    case ToolId::Watercolor: return watercolors();
    case ToolId::Airbrush: return airbrushes();
    case ToolId::Decoration: return decorations();
    default: return {};
    }
}

} // namespace mothion::tools
