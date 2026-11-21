#pragma once

#include "core/puppet/WarpRaster.h"

#include <functional>

namespace mothion::deform {

using puppet::Vec2;

// `source` bent by `map` (a point of the page as drawn to where it goes): a grid of `cell` px laid on it, each vertex
// moved by `map` and each cell that has pixels drawn as two triangles to where its corners went. The image returned is
// placed where it landed (empty if `source` has no pixels).
puppet::RasterImage warpImage(const puppet::RasterImage &source, const std::function<Vec2(Vec2)> &map, int cell = 8);

} // namespace mothion::deform
