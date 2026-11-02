#include "core/fill/FloodFill.h"

#include <algorithm>
#include <cstdlib>

namespace mothion::fill {

namespace {

int difference(doc::Pixel a, doc::Pixel b)
{
    int most = 0;
    for (int shift = 0; shift < 32; shift += 8)
        most = std::max(most, std::abs(int((a >> shift) & 0xff) - int((b >> shift) & 0xff)));
    return most;
}

void include(Region &r, int x, int y)
{
    r.left = std::min(r.left, x);
    r.right = std::max(r.right, x);
    r.top = std::min(r.top, y);
    r.bottom = std::max(r.bottom, y);
}

// One step of growth by a pixel: to the 4 neighbours (`diagonal` false) or
// the 8 (true). Alternating the two makes the growth close to a circle.
void growStep(Region &r, bool diagonal)
{
    const int w = r.width, h = r.height;
    const std::vector<std::uint8_t> from = r.mask;
    const int x0 = std::max(0, r.left - 1), x1 = std::min(w - 1, r.right + 1);
    const int y0 = std::max(0, r.top - 1), y1 = std::min(h - 1, r.bottom + 1);
    auto inside = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && from[std::size_t(y) * std::size_t(w) + std::size_t(x)];
    };
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            std::uint8_t &m = r.mask[std::size_t(y) * std::size_t(w) + std::size_t(x)];
            if (m)
                continue;
            bool hit = inside(x - 1, y) || inside(x + 1, y) || inside(x, y - 1) || inside(x, y + 1);
            if (!hit && diagonal)
                hit = inside(x - 1, y - 1) || inside(x + 1, y - 1) || inside(x - 1, y + 1) || inside(x + 1, y + 1);
            if (hit) {
                m = 255;
                include(r, x, y);
            }
        }
    }
}

} // namespace

Region floodRegion(const doc::Pixel *pixels, int width, int height, int seedX, int seedY, int tolerance)
{
    Region r;
    r.width = width;
    r.height = height;
    r.mask.assign(std::size_t(width) * std::size_t(height), 0);
    r.left = width;
    r.top = height;
    if (seedX < 0 || seedY < 0 || seedX >= width || seedY >= height)
        return r;
    const doc::Pixel seed = pixels[std::size_t(seedY) * std::size_t(width) + std::size_t(seedX)];
    auto fits = [&](int x, int y) {
        const std::size_t i = std::size_t(y) * std::size_t(width) + std::size_t(x);
        return !r.mask[i] && difference(pixels[i], seed) <= tolerance;
    };

    // Scanline fill: a run is filled whole, the rows above and below are
    // searched for the runs that continue it.
    std::vector<std::pair<int, int>> stack{{seedX, seedY}};
    while (!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        if (!fits(x, y))
            continue;
        int left = x, right = x;
        while (left > 0 && fits(left - 1, y))
            --left;
        while (right < width - 1 && fits(right + 1, y))
            ++right;
        std::fill_n(r.mask.begin() + std::ptrdiff_t(std::size_t(y) * std::size_t(width) + std::size_t(left)),
                    right - left + 1, std::uint8_t(255));
        include(r, left, y);
        include(r, right, y);
        for (int ny : {y - 1, y + 1}) {
            if (ny < 0 || ny >= height)
                continue;
            bool inRun = false;
            for (int nx = left; nx <= right; ++nx) {
                const bool f = fits(nx, ny);
                if (f && !inRun)
                    stack.emplace_back(nx, ny);
                inRun = f;
            }
        }
    }
    return r;
}

void grow(Region &region, int radius)
{
    if (region.isEmpty())
        return;
    for (int i = 0; i < radius; ++i)
        growStep(region, i % 2 == 1);
}

} // namespace mothion::fill
