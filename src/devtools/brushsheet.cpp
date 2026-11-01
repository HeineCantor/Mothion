// Developer tool: renders a sheet of test strokes with the real brush engine.
//
//   mothion_brushsheet out.png [brush.sut ...]
//
// One panel per brush (the built-in round brush first), drawn at the brush's
// real size and scaled down to the sheet, with strokes that exercise
// pressure, speed and direction. For brushes with image tips a second panel
// uses the tip thumbnails as drawn instead of the filled interpretation.

#include "canvas/StrokeSession.h"
#include "canvas/TileQt.h"
#include "core/brush/DabGenerator.h"
#include "tools/SutImporter.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>

#include <cmath>
#include <cstdio>
#include <numbers>

using namespace mothion;

namespace {

constexpr int kPanelWidth = 900;
constexpr int kPanelHeight = 330;
constexpr int kLabelHeight = 26;

struct Stroke
{
    QString label;
    std::vector<brush::BrushInput> points;
};

// Test strokes in a 8 × 3.6 unit box, unit = brush diameter.
std::vector<Stroke> testStrokes(double unit)
{
    std::vector<Stroke> strokes;
    auto line = [&](const QString &label, double y, double velocity) {
        Stroke s{label, {}};
        for (int i = 0; i <= 80; ++i) {
            const double t = i / 80.0;
            brush::BrushInput in;
            in.x = unit * (0.6 + t * 3.0);
            in.y = unit * y;
            in.pressure = std::sin(t * std::numbers::pi);
            in.velocity = velocity;
            s.points.push_back(in);
        }
        strokes.push_back(s);
    };
    line(QStringLiteral("pressione 0→1→0, lento"), 0.9, 0.05);
    line(QStringLiteral("pressione 0→1→0, veloce"), 2.7, 0.5);

    Stroke wave{QStringLiteral("onda"), {}};
    for (int i = 0; i <= 120; ++i) {
        const double t = i / 120.0;
        brush::BrushInput in;
        in.x = unit * (4.4 + t * 3.0);
        in.y = unit * (0.9 + std::sin(t * 2.0 * std::numbers::pi) * 0.45);
        in.pressure = 0.8;
        in.velocity = 0.2;
        wave.points.push_back(in);
    }
    strokes.push_back(wave);

    Stroke spiral{QStringLiteral("spirale"), {}};
    for (int i = 0; i <= 160; ++i) {
        const double t = i / 160.0;
        const double a = t * 3.2 * std::numbers::pi;
        const double r = unit * (0.15 + 0.6 * t);
        brush::BrushInput in;
        in.x = unit * 5.9 + std::cos(a) * r;
        in.y = unit * 2.7 + std::sin(a) * r * 0.75;
        in.pressure = 0.6 + 0.4 * t;
        in.velocity = 0.2;
        spiral.points.push_back(in);
    }
    strokes.push_back(spiral);
    return strokes;
}

QImage renderPanel(const QString &title, const tools::BrushPreset &preset, const QList<QImage> &tips)
{
    const tools::BrushDefinitionPtr brushDef = preset.brush ? preset.brush : tools::roundBrush();
    brush::BrushStyle style = brushDef->style;
    style.size = preset.size / 2.0;
    const double unit = preset.size;
    const QRect area(0, 0, int(unit * 8.0), int(unit * 3.6));

    doc::TiledImage image;
    auto stamper = std::make_shared<canvas::DabStamper>(QColor(0x20, 0x20, 0x24), style.hardness, tips,
                                                        style.verticalThickness);
    canvas::StrokeSession session(image, stamper, preset.opacity, false, area);
    std::uint32_t seed = 1;
    for (const Stroke &s : testStrokes(unit)) {
        brush::DabGenerator gen(style, seed++);
        for (std::size_t i = 0; i < s.points.size(); ++i) {
            const auto dabs = i == 0 ? gen.begin(s.points[i]) : gen.continueTo(s.points[i]);
            session.addDabs(dabs);
        }
    }

    QImage panel(kPanelWidth, kPanelHeight + kLabelHeight, QImage::Format_ARGB32_Premultiplied);
    panel.fill(QColor(0xf4, 0xf4, 0xf0));
    QPainter p(&panel);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QImage strokes = canvas::toQImage(image, area);
    const QRectF target(0, kLabelHeight, kPanelWidth, kPanelHeight);
    p.fillRect(target, Qt::white);
    p.drawImage(target, strokes);
    p.setPen(QColor(0x30, 0x30, 0x30));
    QFont f = p.font();
    f.setPixelSize(15);
    p.setFont(f);
    p.drawText(QRect(8, 0, kPanelWidth - 16, kLabelHeight), Qt::AlignVCenter | Qt::AlignLeft, title);
    p.drawText(QRect(8, 0, kPanelWidth - 16, kLabelHeight), Qt::AlignVCenter | Qt::AlignRight,
               QStringLiteral("diametro %1 px, scala 1:%2").arg(preset.size).arg(unit * 8.0 / kPanelWidth, 0, 'f', 1));
    p.setPen(QColor(0xc8, 0xc8, 0xc8));
    p.drawRect(target.adjusted(0, 0, -1, -1));
    return panel;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        std::fprintf(stderr, "usage: mothion_brushsheet out.png [brush.sut ...]\n");
        return 2;
    }

    QList<QImage> panels;
    tools::BrushPreset round;
    round.name = QStringLiteral("Tondo (predefinito)");
    round.size = 40.0;
    panels << renderPanel(round.name, round, {});

    for (const QString &path : args.mid(1)) {
        QString error;
        const auto imported = tools::importSut(path, &error);
        if (!imported) {
            std::fprintf(stderr, "%s: %s\n", qPrintable(path), qPrintable(error));
            continue;
        }
        const tools::BrushPreset &preset = imported->preset;
        QString title = preset.name;
        if (!imported->warnings.isEmpty())
            title += QStringLiteral("   [non riprodotto: %1]").arg(imported->warnings.join(QStringLiteral(", ")));
        panels << renderPanel(title, preset, preset.brush->tips);
        if (!imported->tipThumbnails.isEmpty())
            panels << renderPanel(preset.name + QStringLiteral(" — punte come disegnate (solo contorno)"), preset,
                                  imported->tipThumbnails);
        std::printf("%s: %d punte, %s\n", qPrintable(preset.name), int(preset.brush->tips.size()),
                    imported->warnings.isEmpty() ? "tutto supportato"
                                                 : qPrintable(imported->warnings.join(QStringLiteral("; "))));
    }

    int height = 0;
    for (const QImage &panel : panels)
        height += panel.height() + 10;
    QImage sheet(kPanelWidth + 20, height + 10, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(0xdd, 0xdd, 0xd8));
    QPainter p(&sheet);
    int y = 10;
    for (const QImage &panel : panels) {
        p.drawImage(10, y, panel);
        y += panel.height() + 10;
    }
    p.end();
    if (!sheet.save(args.first())) {
        std::fprintf(stderr, "cannot write %s\n", qPrintable(args.first()));
        return 1;
    }
    std::printf("%s\n", qPrintable(QFileInfo(args.first()).absoluteFilePath()));
    return 0;
}
