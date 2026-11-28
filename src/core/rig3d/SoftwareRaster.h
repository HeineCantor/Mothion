#pragma once

#include "core/rig3d/Skinning.h"

#include <cstdint>
#include <vector>

namespace mothion::rig3d {

// A texture decoded: width × height pixels, ARGB (not premultiplied), rows from the top.
struct TexturePixels
{
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels;
};

// The posed meshes drawn on the CPU into `pixels` (width × height, premultiplied ARGB, cleared to transparent first):
// triangles with a depth buffer, each pixel's color the material's base color (times its texture, at the pixel's UV
// interpolated in perspective) lit by a light from the camera (ambient plus Lambert), with `samples` × `samples`
// samples a pixel for smooth edges. What Mothion draws with where OpenGL cannot be had (a thread with no context,
// tests on a machine with no screen).
void rasterize(const Model &model, const std::vector<PosedPrimitive> &meshes, const Projection &projection,
               const Vec3 &eye, const std::vector<TexturePixels> &textures, int width, int height,
               std::vector<std::uint32_t> &pixels, int samples = 2);

} // namespace mothion::rig3d
