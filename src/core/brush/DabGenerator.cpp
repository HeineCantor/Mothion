#include "core/brush/DabGenerator.h"

#include "core/brush/StrokeShaper.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::brush {

namespace {

constexpr double kRadToDeg = 180.0 / std::numbers::pi;
// A runaway setting (tiny spacing on a long segment) must not freeze the UI.
constexpr int kMaxDabsPerSegment = 20000;

double wrapDegrees(double a)
{
    a = std::fmod(a, 360.0);
    return a < 0.0 ? a + 360.0 : a;
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

double lerpAngle(double a, double b, double t)
{
    double d = std::fmod(b - a, 360.0);
    if (d > 180.0)
        d -= 360.0;
    else if (d < -180.0)
        d += 360.0;
    return wrapDegrees(a + d * t);
}

bool isSoft(const BrushStyle &s) { return s.hardness < 0.99999999 && s.tipCount == 0; }

} // namespace

DabGenerator::DabGenerator(const BrushStyle &style, std::uint32_t seed)
    : m_style(style)
    , m_rng(seed)
{
}

BrushInput DabGenerator::interpolate(const BrushInput &a, const BrushInput &b, double t)
{
    BrushInput r;
    r.x = lerp(a.x, b.x, t);
    r.y = lerp(a.y, b.y, t);
    r.pressure = lerp(a.pressure, b.pressure, t);
    r.velocity = lerp(a.velocity, b.velocity, t);
    r.tilt = lerp(a.tilt, b.tilt, t);
    r.hasTilt = a.hasTilt && b.hasTilt;
    r.penAngle = lerpAngle(a.penAngle, b.penAngle, t);
    r.inOut = lerp(a.inOut, b.inOut, t);
    r.sizeScale = lerp(a.sizeScale, b.sizeScale, t);
    r.flowScale = lerp(a.flowScale, b.flowScale, t);
    r.direction = b.direction;
    return r;
}

BrushInput DabGenerator::shaped(BrushInput in, double distance) const
{
    in.inOut = inOutFactor(m_style.inOut, distance, m_strokeLength);
    in.pressure *= startTaperFactor(m_style.startTaper, distance);
    return in;
}

std::vector<BrushDab> DabGenerator::begin(const BrushInput &first)
{
    m_last = first;
    m_dabIndex = 0;
    m_travelled = 0.0;
    const ColorStyle &c = m_style.color;
    if (c.perStroke) {
        // One shift for the whole stroke: either way for H/S/V, towards the secondary color for the blend.
        m_strokeColor.hue = (2.0 * m_rng.next01() - 1.0) * c.strokeHue;
        m_strokeColor.saturation = (2.0 * m_rng.next01() - 1.0) * c.strokeSaturation;
        m_strokeColor.value = (2.0 * m_rng.next01() - 1.0) * c.strokeValue;
        m_strokeColor.subColor = m_rng.next01() * c.strokeSubColor;
    }
    std::vector<BrushDab> dabs;
    m_remainder = place(shaped(first, 0.0), dabs);
    return dabs;
}

std::vector<BrushDab> DabGenerator::continueTo(const BrushInput &next)
{
    std::vector<BrushDab> dabs;
    const double dx = next.x - m_last.x;
    const double dy = next.y - m_last.y;
    const double length = std::hypot(dx, dy);
    if (length <= 1e-9) {
        // Keep the newest pen state (pressure may change on the spot).
        const double direction = m_last.direction;
        m_last = next;
        m_last.direction = direction;
        return dabs;
    }

    BrushInput target = next;
    target.direction = wrapDegrees(std::atan2(dy, dx) * kRadToDeg);
    double d = m_remainder;
    int count = 0;
    while (d < length && count++ < kMaxDabsPerSegment)
        d += place(shaped(interpolate(m_last, target, d / length), m_travelled + d), dabs);
    m_remainder = std::max(0.0, d - length);
    m_travelled += length;
    m_last = target;
    return dabs;
}

double DabGenerator::place(const BrushInput &in, std::vector<BrushDab> &out)
{
    const Single main = computeDab(in, true);
    ++m_dabIndex;
    if (!m_style.spray) {
        if (!main.skip)
            out.push_back(main.dab);
        return main.spacing;
    }

    // Spray: particles scattered in a disc around the stroke point.
    const double baseRadius = m_style.spraySizeSynced ? m_style.spraySize * main.dab.radius : m_style.spraySize;
    const double radius = std::max(0.0, m_style.spraySizeEffector.evaluate(baseRadius, in, &m_rng)) * in.sizeScale;
    const int particles =
        std::max(1, int(std::lround(m_style.sprayDensityEffector.evaluate(m_style.sprayDensity, in, &m_rng))));
    const double bias = std::clamp(m_style.sprayBias, -1.0, 1.0);
    for (int i = 0; i < particles; ++i) {
        const double u1 = m_rng.next01();
        const double u2 = m_rng.next01();
        // max(u1, u2) is distributed like sqrt(u): even over the disc's area.
        double r = 1.0;
        if (bias <= -1.0)
            r = 1.0;
        else if (bias <= 0.0)
            r = std::max(u1, u2) * (bias + 1.0) - bias; // pushed towards the edge
        else
            r = std::max(u1, u2) * (1.0 - bias) + std::min(u1, u2) * bias * 0.5; // towards the centre
        const double theta = m_rng.next01() * 2.0 * std::numbers::pi;
        BrushInput p = in;
        p.x += std::cos(theta) * r * radius;
        p.y += std::sin(theta) * r * radius;
        Single particle = computeDab(p, true);
        if (!particle.skip)
            out.push_back(particle.dab);
    }
    return main.spacing;
}

DabGenerator::Single DabGenerator::computeDab(const BrushInput &in, bool applyRandom)
{
    const BrushStyle &s = m_style;
    Lcg *rng = applyRandom ? &m_rng : nullptr;
    Single result;
    BrushDab &dab = result.dab;
    dab.x = in.x;
    dab.y = in.y;

    // Size (radius) and the extra room a soft edge needs.
    double radius = s.sizeEffector.evaluate(s.size, in, rng) * in.sizeScale;
    radius = std::max(radius, s.atLeastOnePixel ? 0.51 : 0.1);
    if (isSoft(s))
        radius *= 1.5 - 0.5 * std::clamp(s.hardness, 0.0, 1.0);
    dab.radius = radius;

    double thickness = std::clamp(s.thicknessEffector.evaluate(s.thickness, in, rng), 0.01, 1.0);
    if (radius < 1.0)
        thickness = std::max(thickness, std::min(1.0, 1.0 / radius));
    dab.thickness = thickness;

    // Spacing, in pixels.
    const double fraction = spacingFraction(in, thickness, applyRandom);
    double spacing = 2.0 * std::max(radius, 0.1) * fraction;
    if (s.spacingMode == SpacingMode::Auto || s.spacingMode == SpacingMode::AutoCapped) {
        const double soft = (1.0 - s.hardness) * radius;
        if (radius > 3.0 && soft > 4.0) {
            const double automatic = soft * 0.25;
            spacing = s.spacingMode == SpacingMode::AutoCapped ? std::min(spacing, automatic) : automatic;
        }
    }
    result.spacing = std::max(spacing, thickness < 1.0 ? 0.5 : 1.0);

    // Opacity and flow.
    const double opacity = std::min(1.0, s.dabOpacityEffector.evaluate(s.dabOpacity, in, rng));
    double flow = s.flowEffector.evaluate(s.flow, in, rng) * in.flowScale;
    if (radius < 1.0)
        flow *= radius; // a sub-pixel dab covers less than its alpha says
    if (s.adjustFlowBySpacing && fraction < 0.33)
        flow *= fraction * 3.0;
    if (thickness < 1.0)
        flow /= thickness;
    dab.alpha = std::clamp(flow, 0.0, 1.0) * std::clamp(opacity, 0.0, 1.0);

    // Tip and mirroring.
    double angleExtra = 0.0;
    if (s.tipCount > 0) {
        dab.tip = chooseTip(dab.mirror, angleExtra);
        result.skip = dab.tip < 0;
    }

    // Rotation.
    double angle = 0.0;
    if (s.rotationMode & RotationOn) {
        angle = s.rotation;
        if (s.rotationMode & RotationPenAngle)
            angle += in.penAngle;
        else if (s.rotationMode & RotationDirection)
            angle += in.direction;
        if ((s.rotationMode & RotationRandom) && rng)
            angle += (rng->next01() - 0.5) * s.rotationRandom * 360.0;
    }
    dab.angle = wrapDegrees(angle + angleExtra);

    // Color variations and watercolor mixing.
    const ColorStyle &c = s.color;
    if (c.varies()) {
        dab.color.subColor = std::clamp(c.subColorEffector.evaluate(c.subColor, in, rng) + m_strokeColor.subColor, 0.0, 1.0);
        dab.color.hue = c.hueEffector.evaluate(c.hue, in, rng) + m_strokeColor.hue;
        dab.color.saturation = c.saturationEffector.evaluate(c.saturation, in, rng) + m_strokeColor.saturation;
        dab.color.value = c.valueEffector.evaluate(c.value, in, rng) + m_strokeColor.value;
    }
    dab.color.target = c.target;
    if (s.mix.on) {
        dab.mixAmount = std::clamp(s.mix.amountEffector.evaluate(s.mix.amount, in, rng), 0.0, 1.0);
        dab.mixDensity = std::clamp(s.mix.densityEffector.evaluate(s.mix.density, in, rng), 0.0, 1.0);
    }
    return result;
}

double DabGenerator::spacingFraction(const BrushInput &in, double thickness, bool applyRandom)
{
    const BrushStyle &s = m_style;
    switch (s.spacingMode) {
    case SpacingMode::Narrow:
    case SpacingMode::Normal:
    case SpacingMode::Wide: {
        static constexpr double kBase[] = {0.12, 0.08, 0.04};
        const double base = kBase[int(s.spacingMode) - 1];
        return base * (3.5 - 2.5 * std::max(s.hardness, 0.2)) * std::max(thickness, 0.01);
    }
    case SpacingMode::Custom:
    case SpacingMode::Auto:
    case SpacingMode::AutoCapped:
        break;
    }
    return std::max(0.0, s.spacingEffector.evaluate(s.spacing, in, applyRandom ? &m_rng : nullptr));
}

int DabGenerator::chooseTip(bool &mirror, double &extraAngle)
{
    const BrushStyle &s = m_style;
    const int n = s.tipCount;
    const int k = m_dabIndex;
    int index = 0;
    switch (s.tipOrder) {
    case TipOrder::Cycle: index = k % n; break;
    case TipOrder::PingPong:
        if (n == 1) {
            index = 0;
        } else {
            const int m = k % (2 * n - 2);
            index = m < n - 1 ? m : 2 * n - 2 - m;
        }
        break;
    case TipOrder::HoldLast: index = std::min(k, n - 1); break;
    case TipOrder::Random: index = std::min(n - 1, int(m_rng.next01() * n)); break;
    case TipOrder::Once: index = k < n ? k : -1; break;
    }

    auto flip = [&](std::uint32_t always, std::uint32_t random, std::uint32_t alternate) {
        if (s.tipFlip & always)
            return true;
        if ((s.tipFlip & random) && m_rng.next01() < 0.5)
            return true;
        return (s.tipFlip & alternate) && (k % 2 == 1);
    };
    const bool h = flip(FlipHAlways, FlipHRandom, FlipHAlternate);
    const bool v = flip(FlipVAlways, FlipVRandom, FlipVAlternate);
    // Flipping both ways is a half turn: mirror = H xor V, plus 180° when V.
    mirror = h != v;
    extraAngle = v ? 180.0 : 0.0;
    return index;
}

} // namespace mothion::brush
