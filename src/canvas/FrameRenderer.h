#pragma once

#include "canvas/Resample.h"
#include "core/document/Document.h"

#include <QImage>
#include <QRect>

class QPainter;

namespace mothion::canvas {

// The part of the document the drawing's visible layers cover (tile bounds;
// for a perspective layer, its lines). Without `guides`, perspective layers
// are left out, here and in composeDrawing().
QRect drawingBounds(const doc::Drawing &drawing, bool guides = true);
// The drawing's visible layers composed over transparency in `area`
// (document coordinates), as an image of that size: what its cel shows,
// before the cel's own opacity and blend mode.
QImage composeDrawing(const doc::Drawing &drawing, const QRect &area, bool guides = true);
// The smallest rect holding the pixels the drawing's visible layers show (not tile bounds: exact); empty if they show
// nothing.
QRect visibleBounds(const doc::Drawing &drawing, bool guides = false);

// Draws a perspective layer's grid (and horizon) in document coordinates,
// with the painter's opacity and composition mode.
void paintPerspective(QPainter &p, const perspective::Grid &grid);

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
    // Without guides perspective layers are left out (export, playback).
    void setGuidesVisible(bool visible) { m_guides = visible; }

    // How the pixels of a cel that is moved, turned or scaled by its motion are read (see canvas/Resample.h). The painter's
    // own bilinear (fast) by default; a wider filter for export.
    void setResampleFilter(ResampleFilter filter) { m_filter = filter; }
    // This cel is drawn as it is, without its motion: to draw on it where its pixels are (kNoId: none).
    void setCelAsDrawn(doc::CelId cel) { m_asDrawn = cel; }
    doc::CelId celAsDrawn() const { return m_asDrawn; }
    // Only this cel is drawn (kNoId: all of them), to see what one cel makes of the frame by itself.
    void setOnlyCel(doc::CelId cel) { m_only = cel; }

    // Whole frame (frame changed, cels changed, document resized...).
    void renderAll(const doc::Document &doc, int frame);
    // Only `rect` (document coordinates).
    void render(const doc::Document &doc, int frame, const QRect &rect);

private:
    QImage m_image;
    bool m_paper = true;
    bool m_guides = true;
    ResampleFilter m_filter = ResampleFilter::Bilinear;
    doc::CelId m_asDrawn = doc::kNoId;
    doc::CelId m_only = doc::kNoId;
};

} // namespace mothion::canvas
