#pragma once

#include "core/puppet/WarpRaster.h"
#include "core/tween/Flow.h"

namespace mothion::tween {

// The picture between two others. `a` and `b` are the same block of pixels of the document (the same x, y, width and
// height), and `s` is on that block's grid and says where each point of `a` is in `b`: b(x + s(x)) is a(x), as
// registerImages() finds it. At `t` (0 is `a`, 1 is `b`) each point of `a` has gone t of the way to its place in `b`,
// and both pictures are drawn there, `a` fading out as `b` fades in (parts with no counterpart, where `s` is nothing,
// just fade). Premultiplied pixels, as in puppet::RasterImage.
puppet::RasterImage morph(const puppet::RasterImage &a, const puppet::RasterImage &b, const Field &s, double t);

} // namespace mothion::tween
