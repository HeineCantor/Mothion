#pragma once

#include "core/document/TiledImage.h"
#include "core/document/VectorStroke.h"

#include <QRect>
#include <QTransform>

#include <vector>

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// A vector layer's strokes drawn again from their points, which is what keeps them sharp when the layer is
// scaled or turned: the pixels are made anew at the new size, not stretched.

// `stroke` moved by `transform` (document coordinates): its points go where the transform takes them, the
// direction of travel turns with it, and the brush size follows the scale (the square root of the area scale: a
// round tip stays round; an uneven scale does not squash it).
doc::VectorStroke transformedStroke(const doc::VectorStroke &stroke, const QTransform &transform);

// Draws `strokes`, in order, onto `target` (the layer's pixels; empty to make the layer from them), each as it
// was drawn: its brush (looked up by name in `state`), size, colors, seed and points. What reads the pixels
// under the stroke (color mixing, the water edge) is off, as it is on a vector layer. Nothing is painted
// outside `clip` (empty: no limit). Returns the area painted.
QRect drawStrokes(doc::TiledImage &target, const std::vector<doc::VectorStroke> &strokes, const tools::ToolState &state,
                  const QRect &clip);

} // namespace mothion::canvas
