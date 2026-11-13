#pragma once

#include "core/document/VectorStroke.h"

#include <QPointF>

#include <vector>

namespace mothion::canvas {

// What the vector eraser takes of the strokes it touches (as in Clip Studio's vector eraser).
enum class VectorEraseMode {
    Part,           // only what the eraser's circle covers
    ToIntersection, // from the touched place to the nearest crossing with another stroke on each side
    WholeLine,      // the whole stroke
};

struct VectorErasure
{
    std::vector<doc::VectorStroke> strokes; // what is left, in the same order (a cut stroke becomes its pieces)
    bool changed = false;
};

// `strokes` after an eraser of `radius` went along `path` (document coordinates). A stroke is touched where its
// line comes within `radius` plus half its width of the path. Strokes that erase (the eraser's own) are left alone.
// A cut stroke ends where it was cut, at a point put on the line.
VectorErasure eraseFromStrokes(const std::vector<doc::VectorStroke> &strokes, const std::vector<QPointF> &path,
                               double radius, VectorEraseMode mode);

} // namespace mothion::canvas
