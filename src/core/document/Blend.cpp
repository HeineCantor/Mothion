#include "core/document/Blend.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mothion::doc {

namespace {

using Rgb = std::array<float, 3>;

constexpr float kEps = 0.001f;

float lum(const Rgb &c) { return 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; }

Rgb clipColor(Rgb c)
{
    const float l = lum(c);
    const float lo = std::min({c[0], c[1], c[2]});
    const float hi = std::max({c[0], c[1], c[2]});
    for (float &v : c) {
        if (lo < 0.0f)
            v = l + (v - l) * l / (l - lo);
        if (hi > 1.0f)
            v = l + (v - l) * (1.0f - l) / (hi - l);
    }
    return c;
}

Rgb setLum(Rgb c, float l)
{
    const float d = l - lum(c);
    for (float &v : c)
        v += d;
    return clipColor(c);
}

float sat(const Rgb &c) { return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]}); }

Rgb setSat(Rgb c, float s)
{
    int hi = 0, lo = 0;
    for (int i = 1; i < 3; ++i) {
        if (c[std::size_t(i)] > c[std::size_t(hi)])
            hi = i;
        if (c[std::size_t(i)] < c[std::size_t(lo)])
            lo = i;
    }
    if (hi == lo)
        return {0.0f, 0.0f, 0.0f}; // no hue to give the saturation to
    const int mid = 3 - hi - lo;
    const float range = c[std::size_t(hi)] - c[std::size_t(lo)];
    Rgb out{};
    out[std::size_t(mid)] = (c[std::size_t(mid)] - c[std::size_t(lo)]) * s / range;
    out[std::size_t(hi)] = s;
    out[std::size_t(lo)] = 0.0f;
    return out;
}

float burn(float f, float b) { return 1.0f - std::min(1.0f, (1.0f - b) / std::max(f, kEps)); }
float dodge(float f, float b) { return std::min(1.0f, b / std::max(1.0f - f, kEps)); }

// The color of the mode, f over b, both straight (not premultiplied).
Rgb blendColor(BlendMode mode, const Rgb &f, const Rgb &b)
{
    auto each = [&](auto fn) { return Rgb{fn(f[0], b[0]), fn(f[1], b[1]), fn(f[2], b[2])}; };
    switch (mode) {
    case BlendMode::LinearBurn: return each([](float x, float y) { return std::max(0.0f, x + y - 1.0f); });
    case BlendMode::Subtract: return each([](float x, float y) { return std::max(0.0f, y - x); });
    case BlendMode::Divide: return each([](float x, float y) { return std::min(1.0f, y / std::max(x, kEps)); });
    case BlendMode::LinearLight:
        return each([](float x, float y) { return std::clamp(2.0f * x + y - 1.0f, 0.0f, 1.0f); });
    case BlendMode::VividLight:
        return each([](float x, float y) {
            if (x <= 0.0f)
                return 0.0f;
            if (x >= 1.0f)
                return 1.0f;
            return x < 0.5f ? burn(2.0f * x, y) : dodge(2.0f * x - 1.0f, y);
        });
    case BlendMode::PinLight:
        return each([](float x, float y) { return x < 0.5f ? std::min(2.0f * x, y) : std::max(2.0f * x - 1.0f, y); });
    case BlendMode::HardMix:
        return each([](float x, float y) { return x + y - (y < 0.5f ? 1.0f / 256.0f : 0.0f) >= 1.0f ? 1.0f : 0.0f; });
    case BlendMode::LighterColor: return lum(f) >= lum(b) ? f : b;
    case BlendMode::DarkerColor: return lum(f) <= lum(b) ? f : b;
    case BlendMode::Hue: return setLum(setSat(f, sat(b)), lum(b));
    case BlendMode::Saturation: return setLum(setSat(b, sat(f)), lum(b));
    case BlendMode::Color: return setLum(f, lum(b));
    case BlendMode::Luminosity: return setLum(b, lum(f));
    default: return f; // the modes QPainter does
    }
}

float mix(float x, float y, float t) { return x + (y - x) * t; }

Pixel pack(const Rgb &c, float alpha)
{
    auto byte = [&](float v) { return Pixel(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return (byte(alpha) << 24) | (byte(c[0] * alpha) << 16) | (byte(c[1] * alpha) << 8) | byte(c[2] * alpha);
}

} // namespace

bool needsSoftwareBlend(BlendMode mode) { return int(mode) >= int(BlendMode::LinearBurn); }

void blendSpan(Pixel *dst, const Pixel *src, int count, BlendMode mode, float opacity)
{
    for (int i = 0; i < count; ++i) {
        const Pixel s = src[i];
        const float sa0 = float(s >> 24) / 255.0f;
        const float fa = sa0 * opacity;
        if (fa <= 0.0f)
            continue;
        const Rgb f{float((s >> 16) & 0xff) / 255.0f / sa0, float((s >> 8) & 0xff) / 255.0f / sa0,
                    float(s & 0xff) / 255.0f / sa0};
        const Pixel d = dst[i];
        const float ba = float(d >> 24) / 255.0f;
        if (ba < kEps) {
            dst[i] = pack(f, fa);
            continue;
        }
        const Rgb b{float((d >> 16) & 0xff) / 255.0f / ba, float((d >> 8) & 0xff) / 255.0f / ba,
                    float(d & 0xff) / 255.0f / ba};
        const float a = fa + ba - fa * ba;
        const float r = fa / a;
        const float t = ba / a;

        Rgb out{};
        for (std::size_t k = 0; k < 3; ++k) {
            if (mode == BlendMode::GlowDodge) {
                const float lit = dodge(f[k] * fa, b[k]); // the source alpha says how much it lights
                out[k] = mix(f[k], lit, t);
            } else if (mode == BlendMode::AddGlow) {
                const float sum = std::min(mix(b[k], b[k] + f[k], r), 1.0f); // clipped after the mix
                out[k] = mix(f[k], sum, t);
            }
        }
        if (mode != BlendMode::GlowDodge && mode != BlendMode::AddGlow) {
            const Rgb pure = blendColor(mode, f, b);
            for (std::size_t k = 0; k < 3; ++k)
                out[k] = mix(f[k], mix(b[k], pure[k], r), t);
        }
        dst[i] = pack(out, a);
    }
}

} // namespace mothion::doc
