#pragma once

#include "core/puppet/PuppetRig.h"

#include <cstdint>
#include <vector>

namespace mothion::puppet {

// A block of premultiplied ARGB pixels (0xAARRGGBB) placed in the document.
struct RasterImage
{
    int x = 0, y = 0;
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels; // width × height, row by row

    RasterImage() = default;
    RasterImage(int x0, int y0, int w, int h)
        : x(x0), y(y0), width(w), height(h), pixels(std::size_t(w) * std::size_t(h), 0)
    {
    }
    bool isEmpty() const { return width <= 0 || height <= 0; }
};

// Document area covered by the rig's deformed meshes (whole pixels).
void deformedBounds(const PuppetRig &rig, int &x, int &y, int &width, int &height);

// Draws every triangle of the rig's meshes from `source` (the undeformed
// pixels) to its deformed place in `target`, source-over, mesh by mesh and
// triangle by triangle (later ones on top where the shape folds over
// itself). Each triangle maps back to the source with its own affine
// transform; pixels are sampled bilinearly.
void warp(const PuppetRig &rig, const RasterImage &source, RasterImage &target);

} // namespace mothion::puppet
