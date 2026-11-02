#pragma once

#include "core/document/Document.h"

#include <QImage>
#include <QRect>

namespace mothion::canvas {

// The part of the document the drawing's visible layers cover (tile bounds).
QRect drawingBounds(const doc::Drawing &drawing);
// The drawing's visible layers composed over transparency in `area`
// (document coordinates), as an image of that size: what its cel shows,
// before the cel's own opacity and blend mode.
QImage composeDrawing(const doc::Drawing &drawing, const QRect &area);

// Composes what a frame looks like: paper, then the visible cels from
// bottom to top, each one the drawing it exposes at that frame. A drawing's
// visible layers are composed together first, with their opacity and blend
// mode, like a folder; the cel's opacity and blend mode then put the result
// over the cels below. The result is a document-sized image the canvas
// shows through its view transform.
//
// render() can redo just a rectangle, so a stroke only recomposes the
// tiles it touched.
class FrameRenderer
{
public:
    const QImage &image() const { return m_image; }

    // Without paper the frame starts transparent (PNG export).
    void setPaperVisible(bool visible) { m_paper = visible; }

    // Whole frame (frame changed, cels changed, document resized...).
    void renderAll(const doc::Document &doc, int frame);
    // Only `rect` (document coordinates).
    void render(const doc::Document &doc, int frame, const QRect &rect);

private:
    QImage m_image;
    bool m_paper = true;
};

} // namespace mothion::canvas
