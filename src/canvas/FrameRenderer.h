#pragma once

#include "core/document/Document.h"

#include <QImage>
#include <QRect>

namespace mothion::canvas {

// Composes what a frame looks like: paper, then the visible layers from
// bottom to top (the drawing each one exposes at that frame) with their
// opacity and blend mode. The result is a document-sized image the canvas
// shows through its view transform.
//
// render() can redo just a rectangle, so a stroke only recomposes the
// tiles it touched.
class FrameRenderer
{
public:
    const QImage &image() const { return m_image; }

    // Whole frame (frame changed, layers changed, document resized...).
    void renderAll(const doc::Document &doc, int frame);
    // Only `rect` (document coordinates).
    void render(const doc::Document &doc, int frame, const QRect &rect);

private:
    QImage m_image;
};

} // namespace mothion::canvas
