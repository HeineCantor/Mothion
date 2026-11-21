#pragma once

#include "core/document/Tile.h"
#include "core/fill/FloodFill.h"

#include <cstdint>
#include <vector>

namespace mothion::fill {

// A fill grown under the lines round it up to their middle, as the shape of colour of a vector layer is made: each
// pixel of a line (one that does not look like the clicked one, within `tolerance`) goes to the side it is nearest,
// the area itself (`region`) or what lies beyond the line (pixels that look like the clicked one but are not in the
// area), so the edge of the shape is where the line's middle is and stays under the line however it is moved. A
// line with nothing beyond it within `reach` pixels (the canvas's edge) is taken up to `reach`.
Region grownToLineMiddle(const Region &region, const doc::Pixel *pixels, doc::Pixel seed, int tolerance, int reach);

// The outlines of what is inside a region (its outer edges and those of its holes), along the edges of its pixels,
// in the region's pixel coordinates (a pixel (x, y) is the square from (x, y) to (x + 1, y + 1)). Filled by the
// even-odd rule they give the region back. Each comes without repeating its first point, the turns only (no points
// in the middle of a straight edge).
struct Point
{
    double x = 0, y = 0;
    bool operator==(const Point &) const = default;
};
std::vector<std::vector<Point>> regionOutlines(const Region &region);

} // namespace mothion::fill
