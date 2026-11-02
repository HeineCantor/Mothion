#pragma once

#include "core/document/Tile.h"

#include <cstdint>
#include <vector>

namespace mothion::fill {

// A fill region over a width × height raster: 255 inside, 0 outside.
struct Region
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;
    // Bounds of what is inside; empty (right < left) when nothing is.
    int left = 0, top = 0, right = -1, bottom = -1;

    bool isEmpty() const { return right < left; }
    std::uint8_t at(int x, int y) const { return mask[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
};

// The pixels connected to the seed (4-neighbours) whose color is within
// `tolerance` of the seed's: the largest difference of any channel,
// alpha included, 0..255. `pixels` is premultiplied ARGB, row after row.
Region floodRegion(const doc::Pixel *pixels, int width, int height, int seedX, int seedY, int tolerance);

// Grows the region by `radius` pixels all around (roughly round), so a fill
// slips under the soft edge of the lines that stopped it.
void grow(Region &region, int radius);

} // namespace mothion::fill
