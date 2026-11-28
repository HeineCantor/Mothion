#pragma once

#include "canvas/Resample.h"
#include "core/puppet/WarpRaster.h"
#include "core/document/Document.h"

#include <QImage>
#include <QRect>

#include <map>
#include <optional>
#include <vector>

class QPainter;

namespace mothion::tools {
class ToolState;
}

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
// over the cels below.
//
// The image is of an area of the document (the page, for export; what the canvas shows, which may lie out of the page as
// the canvas has no end), at a scale (1, or a half, a quarter... to show it zoomed out without a picture too big for
// memory). render() can redo just a rectangle, so a stroke only recomposes the tiles it touched.
class FrameRenderer
{
public:
    const QImage &image() const { return m_image; }
    // The part of the document the image is of, and how much smaller it is (1: a pixel for a pixel).
    QRect area() const { return m_area; }
    int step() const { return m_step; }

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
    // This cel is drawn without what the deformation cel that bends it does (its pose and trajectory still move it): to
    // draw on it where its pixels are (kNoId: none).
    void setCelUndeformed(doc::CelId cel) { m_undeformed = cel; }
    // The brushes (looked up by name), to draw the strokes of a vector layer again as a deformer bends them: then a vector
    // layer bends stroke by stroke (lines as wide as the bend makes them, nothing blurred), each raster layer by itself.
    // Without them (nullptr) they are the default ones; without those either a bent drawing bends as the pixels its
    // layers show together.
    void setBrushes(const tools::ToolState *brushes)
    {
        m_brushes = brushes;
        m_deformed.clear();
    }
    // The brushes of every renderer that is given none (the export's, the playback's...), so that what they draw is what
    // the canvas shows. Not owned: forgotten when it goes.
    static void setDefaultBrushes(const tools::ToolState *brushes);
    doc::CelId celUndeformed() const { return m_undeformed; }
    // Only this cel is drawn (kNoId: all of them), to see what one cel makes of the frame by itself.
    void setOnlyCel(doc::CelId cel) { m_only = cel; }
    // This cel shows this drawing whatever its timing says (a mouth of a mouth chart drawn off the timeline); kNoId: as
    // the timing says.
    void setDrawingShown(doc::CelId cel, doc::DrawingId drawing)
    {
        m_shownCel = cel;
        m_shownDrawing = drawing;
    }

    // The whole page, a pixel for a pixel (frame changed, cels changed, document resized...).
    void renderAll(const doc::Document &doc, int frame);
    // The image made of `area`, a pixel of it for `step` × `step` of the document (a power of two; the area is put on
    // whole steps): what it shares with the image there was is kept when the step is the same, the rest is drawn.
    // Without `keep` all of it is drawn (another frame).
    void setArea(const doc::Document &doc, int frame, const QRect &area, int step = 1, bool keep = true);
    // Draws all of the area again.
    void renderArea(const doc::Document &doc, int frame) { render(doc, frame, m_area); }
    // Only `rect` (document coordinates).
    void render(const doc::Document &doc, int frame, const QRect &rect);

private:
    // Where the visible cels show something on the frame (posed as they are).
    QRect contentBounds(const doc::Document &doc, int frame) const;
    // `rect` of the document drawn on `target`, whose pixel (0, 0) is the document's `origin` (a pixel for a pixel).
    void compose(QImage &target, const QPoint &origin, const doc::Document &doc, int frame, const QRect &rect) const;

    QImage m_image;
    QRect m_area;
    int m_step = 1;
    bool m_paper = true;
    bool m_guides = true;
    ResampleFilter m_filter = ResampleFilter::Bilinear;
    doc::CelId m_asDrawn = doc::kNoId;
    doc::CelId m_only = doc::kNoId;
    doc::CelId m_shownCel = doc::kNoId;
    doc::DrawingId m_shownDrawing = doc::kNoId;
    // The drawing the cel shows on the frame (see setDrawingShown()), its time layers moved or bent by what they follow of
    // their own (see timeMoved()).
    const doc::Drawing *drawingOf(const doc::Document &doc, const doc::Cel &cel, int frame) const;
    // `drawing` with each time layer of `cel` that follows a trajectory or a deformer of its own (doc::TimeLayer) moved or
    // bent by it, before the cel's own motion; `drawing` itself if none does. Kept for each cel, and made again only when
    // the drawing's layers (their tiles) or what moves the time layers change. Not bent while the cel is drawn undeformed.
    const doc::Drawing *timeMoved(const doc::Document &doc, const doc::Cel &cel, const doc::Drawing &drawing,
                                  int frame) const;
    struct Moved
    {
        struct How // what moves a time layer on the frame
        {
            doc::LayerId layer = doc::kNoId;
            doc::Matrix2x3 matrix;
            doc::DeformerKind kind = doc::DeformerKind::None;
            doc::Deformer deformer;
            std::vector<double> pose;
            bool operator==(const How &o) const
            {
                const doc::Matrix2x3 &m = matrix, &n = o.matrix;
                return layer == o.layer && m.a == n.a && m.b == n.b && m.c == n.c && m.d == n.d && m.e == n.e && m.f == n.f
                       && kind == o.kind && deformer == o.deformer && pose == o.pose;
            }
        };
        doc::DrawingId drawing = doc::kNoId;
        std::vector<doc::TiledImage> sources; // the drawing's layers' pixels as they were
        std::vector<std::optional<doc::TiledImage>> masks;
        std::vector<How> hows;
        bool brushes = false;
        doc::Drawing result;
    };
    mutable std::map<doc::CelId, Moved> m_moved;

    // A drawing bent by the deformation cel that bends its cel, placed where it landed.
    struct Deformed
    {
        QImage image;
        QRect rect;
    };
    // `drawing` of `cel` bent on `frame`; nullptr if nothing bends it there. Kept for each cel, and made again only when
    // what it is made of changes: the drawing's layers (their tiles: the copies kept here make a tile written to be a new
    // one), the deformer's bones or its pose.
    const Deformed *deformed(const doc::Document &doc, const doc::Cel &cel, const doc::Drawing &drawing, int frame) const;
    // A 3D model cel's model as its pose on `frame` puts it, drawn on the page (cut to what it covers); nullptr for a cel
    // with no model, or one that covers nothing. Kept for each cel, made again only when its model, its settings, its
    // pose or the page's size change.
    const Deformed *modelImage(const doc::Document &doc, const doc::Cel &cel, int frame) const;
    struct ModelShot
    {
        doc::ModelRig rig;
        std::vector<double> pose;
        QSize size;
        bool empty = true;
        Deformed result;
    };
    mutable std::map<doc::CelId, ModelShot> m_models;
    doc::CelId m_undeformed = doc::kNoId;
    const tools::ToolState *m_brushes = nullptr;
    struct Bent
    {
        struct Source
        {
            doc::LayerProperties properties;
            doc::LayerKind kind = doc::LayerKind::Raster;
            doc::TiledImage image;
            std::optional<doc::TiledImage> mask;
        };
        std::vector<Source> layers;
        doc::Deformer deformer;
        std::vector<double> pose;
        bool guides = true;
        puppet::RasterImage source; // the drawing as drawn, its layers composed
        Deformed result;
    };
    mutable std::map<doc::CelId, Bent> m_deformed;
};

} // namespace mothion::canvas
