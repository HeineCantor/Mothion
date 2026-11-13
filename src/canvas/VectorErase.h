#pragma once

#include "core/document/VectorStroke.h"

#include <QPointF>
#include <QRectF>

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
//
// For an eraser that goes on: the strokes as the eraser has left them so far, kept between calls so that each new
// stretch of the path costs only what it touches (the crossings that ToIntersection stops at are those of the
// strokes as they were drawn, `original`).
class VectorEraseState
{
public:
    explicit VectorEraseState(std::vector<doc::VectorStroke> original);

    // The eraser went along `path` (its new stretch). `pressed`: the pen has just come down here (the first stretch of a
    // drag, or the eraser used once). Up to the intersection, a stroke is then taken if the pen came down on its ink;
    // while it goes on, only if the path crosses its line (a pen that slides over the ink of a line it is not on, near a
    // crossing, takes nothing of it).
    void erase(const std::vector<QPointF> &path, double radius, VectorEraseMode mode, bool pressed = true);

    // What is left, in order.
    std::vector<doc::VectorStroke> strokes() const;
    bool changed() const { return m_changed; }
    // The area (document coordinates) whose strokes changed since the last call: it is to be drawn again, with the
    // strokes that reach into it (see strokesIn()). Null if nothing changed.
    QRectF takeDirty();
    // The strokes, in order, whose line comes within the width of `area`.
    std::vector<doc::VectorStroke> strokesIn(const QRectF &area) const;

private:
    struct Entry
    {
        doc::VectorStroke stroke;
        std::size_t origin; // the stroke of `original` it comes from
        QRectF box;         // around its points and width
    };
    std::vector<doc::VectorStroke> m_original;
    std::vector<QRectF> m_originalBoxes;
    std::vector<Entry> m_left;
    QRectF m_dirty;
    bool m_hasDirty = false;
    bool m_changed = false;
};

VectorErasure eraseFromStrokes(const std::vector<doc::VectorStroke> &strokes, const std::vector<QPointF> &path,
                               double radius, VectorEraseMode mode);

} // namespace mothion::canvas
