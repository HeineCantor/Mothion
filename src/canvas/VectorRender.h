#pragma once

#include "canvas/SelectionMask.h"
#include "core/document/TiledImage.h"
#include "core/document/VectorStroke.h"

#include <QRect>
#include <QTransform>

#include <vector>

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// How far out of the canvas the pixels of a layer are kept when they are made again (a layer moved, scaled or warped
// partly out of the canvas keeps what is out of it, as a raster layer does); past it they are let go, to keep a big
// scale from asking for a picture that does not fit memory.
inline constexpr int kBeyondPage = 4096;
// The page, and that much around it: where strokes are drawn again.
inline QRect workArea(const QRect &page)
{
    return page.adjusted(-kBeyondPage, -kBeyondPage, kBeyondPage, kBeyondPage);
}

// A vector layer's strokes drawn again from their points, which is what keeps them sharp when the layer is
// scaled or turned: the pixels are made anew at the new size, not stretched.

// `stroke` moved by `transform` (document coordinates): its points go where the transform takes them, the
// direction of travel follows it (turned by a rotation, mirrored by a flip), and the brush size follows the scale (the square root of the area scale: a
// round tip stays round; an uneven scale does not squash it).
doc::VectorStroke transformedStroke(const doc::VectorStroke &stroke, const QTransform &transform);

// Draws `strokes`, in order, onto `target` (the layer's pixels; empty to make the layer from them), each as it
// was drawn: its brush (looked up by name in `state`), size, colors, seed and points. What reads the pixels
// under the stroke (color mixing, the water edge) is off, as it is on a vector layer. Nothing is painted
// outside `clip` (empty: no limit). Returns the area painted.
QRect drawStrokes(doc::TiledImage &target, const std::vector<doc::VectorStroke> &strokes, const tools::ToolState &state,
                  const QRect &clip);

// A stroke cut along a selection: the runs of its points inside the mask and those outside, each a stroke of its
// own with the stroke's brush, colors and seed. Where the path crosses the outline a point is put on it (found
// to a fraction of a pixel), so the pieces end at the selection's edge. A stroke wholly in or out comes back whole.
struct SplitStroke
{
    std::vector<doc::VectorStroke> inside, outside;
};
SplitStroke splitStrokeByMask(const doc::VectorStroke &stroke, const SelectionMask &mask);

} // namespace mothion::canvas
