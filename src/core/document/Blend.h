#pragma once

#include "core/document/Drawing.h"
#include "core/document/Tile.h"

namespace mothion::doc {

// True for the modes QPainter has no composition mode for: they are blended with blendSpan().
bool needsSoftwareBlend(BlendMode mode);

// Puts `count` pixels of `src` over `dst` with the blend mode and an opacity (0..1). Both are premultiplied ARGB, like
// Pixel everywhere. Where the source is over transparency it lands as it is; elsewhere the color is the blend of the
// two (B), taken the way most programs do: mix(src, mix(dst, B, srcAlpha/A), dstAlpha/A), A being the union of the
// alphas. Glow Dodge lets the source alpha weigh into the dodge instead, and Add (Glow) clips the sum after mixing.
void blendSpan(Pixel *dst, const Pixel *src, int count, BlendMode mode, float opacity);

} // namespace mothion::doc
