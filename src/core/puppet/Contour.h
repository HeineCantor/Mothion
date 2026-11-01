#pragma once

#include "core/puppet/Geometry.h"

#include <cstdint>
#include <vector>

namespace mothion::puppet {

// A binary image: 1 = part of the shape. Pixel (x, y) covers the square
// [originX + x, originX + x + 1) × [originY + y, ...) in document coordinates.
struct Bitmask
{
    int originX = 0, originY = 0;
    int width = 0, height = 0;
    std::vector<std::uint8_t> bits; // width × height, row by row

    Bitmask() = default;
    Bitmask(int x, int y, int w, int h)
        : originX(x), originY(y), width(w), height(h), bits(std::size_t(w) * std::size_t(h), 0)
    {
    }

    bool at(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < width && y < height && bits[std::size_t(y) * std::size_t(width) + std::size_t(x)];
    }
    void set(int x, int y, bool on) { bits[std::size_t(y) * std::size_t(width) + std::size_t(x)] = on ? 1 : 0; }
    bool isEmpty() const;
};

// Grows the shape by `radius` pixels (a round brush), adding room around it.
Bitmask dilated(const Bitmask &mask, int radius);

// One closed outline per connected part (8-connectivity) of the mask,
// traced along its boundary pixels with Moore neighbour tracing; holes are
// not traced (the mesh covers them). Points are pixel centers in document
// coordinates, in tracing order, without repeating the first.
std::vector<std::vector<Vec2>> outerContours(const Bitmask &mask);

// Fewer points along an outline that stays within `tolerance` px of the
// original (Douglas–Peucker on the closed outline). Where the result would
// cross itself, the original points of the offending edges come back.
std::vector<Vec2> simplifyContour(const std::vector<Vec2> &contour, double tolerance);

} // namespace mothion::puppet
