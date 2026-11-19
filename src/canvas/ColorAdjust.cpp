#include "canvas/ColorAdjust.h"

#include "core/Parallel.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mothion::canvas {

namespace {

// A share (0..1) moved by `percent` (-100..100) towards 0 or 1.
double towards(double x, int percent)
{
    const double t = std::clamp(percent, -100, 100) / 100.0;
    return t >= 0 ? x + (1.0 - x) * t : x * (1.0 + t);
}

// Red, green and blue (0..1) shifted, through hue (0..360), saturation and value.
void shiftRgb(double &r, double &g, double &b, const HsvShift &shift)
{
    const double maxC = std::max({r, g, b}), minC = std::min({r, g, b}), chroma = maxC - minC;
    double h = 0;
    if (chroma > 1e-12) {
        if (maxC == r)
            h = 60.0 * std::fmod((g - b) / chroma + 6.0, 6.0);
        else if (maxC == g)
            h = 60.0 * ((b - r) / chroma + 2.0);
        else
            h = 60.0 * ((r - g) / chroma + 4.0);
    }
    double s = maxC > 1e-12 ? chroma / maxC : 0.0;
    double v = maxC;
    h = std::fmod(h + shift.hue + 360.0 * 2, 360.0);
    s = towards(s, shift.saturation);
    v = towards(v, shift.value);
    const double c = v * s, x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0)), m = v - c;
    double rr = 0, gg = 0, bb = 0;
    switch (int(h / 60.0) % 6) {
    case 0: rr = c, gg = x; break;
    case 1: rr = x, gg = c; break;
    case 2: gg = c, bb = x; break;
    case 3: gg = x, bb = c; break;
    case 4: rr = x, bb = c; break;
    default: rr = c, bb = x; break;
    }
    r = rr + m;
    g = gg + m;
    b = bb + m;
}

std::uint32_t channel(double x) { return std::uint32_t(std::lround(std::clamp(x, 0.0, 1.0) * 255.0)); }

} // namespace

std::uint32_t shiftedColor(std::uint32_t argb, const HsvShift &shift)
{
    if (shift.isNone())
        return argb;
    double r = ((argb >> 16) & 0xFF) / 255.0, g = ((argb >> 8) & 0xFF) / 255.0, b = (argb & 0xFF) / 255.0;
    shiftRgb(r, g, b, shift);
    return (argb & 0xFF000000u) | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

doc::Pixel shiftedPixel(doc::Pixel pixel, const HsvShift &shift)
{
    const std::uint32_t a = pixel >> 24;
    if (a == 0 || shift.isNone())
        return pixel;
    // The color without the alpha (the pixel holds it multiplied by it), shifted, multiplied again.
    const double alpha = a / 255.0;
    double r = std::min(1.0, ((pixel >> 16) & 0xFF) / 255.0 / alpha);
    double g = std::min(1.0, ((pixel >> 8) & 0xFF) / 255.0 / alpha);
    double b = std::min(1.0, (pixel & 0xFF) / 255.0 / alpha);
    shiftRgb(r, g, b, shift);
    return (a << 24) | (channel(r * alpha) << 16) | (channel(g * alpha) << 8) | channel(b * alpha);
}

doc::TiledImage shiftedImage(const doc::TiledImage &image, const HsvShift &shift)
{
    if (shift.isNone())
        return image;
    const std::vector<doc::TileCoord> coords = image.tileCoords();
    std::vector<std::shared_ptr<doc::Tile>> tiles(coords.size());
    parallelFor(int(coords.size()), [&](int i) {
        auto tile = std::make_shared<doc::Tile>(*image.tile(coords[std::size_t(i)]));
        for (doc::Pixel &p : tile->pixels)
            p = shiftedPixel(p, shift);
        tiles[std::size_t(i)] = std::move(tile);
    });
    doc::TiledImage out;
    for (std::size_t i = 0; i < coords.size(); ++i)
        out.setTile(coords[i], std::move(tiles[i]));
    return out;
}

} // namespace mothion::canvas
