#include "core/tween/Morph.h"

#include <algorithm>
#include <cmath>

namespace mothion::tween {

namespace {

struct Pixel
{
    float a = 0, r = 0, g = 0, b = 0; // premultiplied
};

Pixel pixelAt(const puppet::RasterImage &image, int x, int y)
{
    if (x < 0 || y < 0 || x >= image.width || y >= image.height)
        return {};
    const std::uint32_t p = image.pixels[std::size_t(y) * std::size_t(image.width) + std::size_t(x)];
    return {float(p >> 24), float((p >> 16) & 0xff), float((p >> 8) & 0xff), float(p & 0xff)};
}

// Bilinear; transparent outside the block.
Pixel sample(const puppet::RasterImage &image, double x, double y)
{
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = float(x - x0), fy = float(y - y0);
    const Pixel p00 = pixelAt(image, x0, y0), p10 = pixelAt(image, x0 + 1, y0);
    const Pixel p01 = pixelAt(image, x0, y0 + 1), p11 = pixelAt(image, x0 + 1, y0 + 1);
    auto mix = [&](float Pixel::*c) {
        const float top = p00.*c * (1 - fx) + p10.*c * fx;
        const float bottom = p01.*c * (1 - fx) + p11.*c * fx;
        return top * (1 - fy) + bottom * fy;
    };
    return {mix(&Pixel::a), mix(&Pixel::r), mix(&Pixel::g), mix(&Pixel::b)};
}

std::uint32_t pack(const Pixel &p)
{
    auto byte = [](float v) { return std::uint32_t(std::clamp(int(std::lround(v)), 0, 255)); };
    const std::uint32_t a = byte(p.a);
    // Premultiplied: no channel above the alpha.
    return a << 24 | std::min(byte(p.r), a) << 16 | std::min(byte(p.g), a) << 8 | std::min(byte(p.b), a);
}

} // namespace

puppet::RasterImage morph(const puppet::RasterImage &a, const puppet::RasterImage &b, const Field &s, double t)
{
    puppet::RasterImage out(a.x, a.y, a.width, a.height);
    if (a.width != b.width || a.height != b.height || a.isEmpty())
        return out;
    t = std::clamp(t, 0.0, 1.0);
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x) {
            // The point of `a` that is at (x, y) at time t: x0 + t s(x0) = (x, y), by a few steps from the guess.
            double px = x, py = y, dx = 0, dy = 0;
            for (int i = 0; i < 4; ++i) {
                s.sample(px, py, dx, dy);
                px = x - t * dx;
                py = y - t * dy;
            }
            s.sample(px, py, dx, dy);
            const Pixel fromA = sample(a, px, py);
            const Pixel fromB = sample(b, px + dx, py + dy);
            Pixel mixed;
            mixed.a = float(fromA.a * (1 - t) + fromB.a * t);
            mixed.r = float(fromA.r * (1 - t) + fromB.r * t);
            mixed.g = float(fromA.g * (1 - t) + fromB.g * t);
            mixed.b = float(fromA.b * (1 - t) + fromB.b * t);
            out.pixels[std::size_t(y) * std::size_t(a.width) + std::size_t(x)] = pack(mixed);
        }
    return out;
}

} // namespace mothion::tween
