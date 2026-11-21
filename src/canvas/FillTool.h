#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/Tile.h"
#include "core/document/TiledImage.h"
#include "core/document/VectorStroke.h"
#include "document/DocumentController.h"

#include <QImage>
#include <QPolygonF>
#include <QTransform>

#include <functional>
#include <vector>

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// The paint bucket: a press fills the area around it with the main color,
// behind what is already drawn there, in the drawing at the current layer and frame (made if the frame is
// empty), as one undo step. The area is found in the current layer or in
// the frame as shown, by the tool's fill settings, and kept inside the
// selection if there is one. The canvas has no end: an area that does not close stops where the canvas is seen.
//
// In lasso mode (FillSettings::Mode::Lasso) a drag draws an outline instead,
// shown open while drawn; the release fills its inside, over what is there.
//
// While the symmetry is on (see `copies`), the fill is made round the axes too, in the same undo step: the bucket also
// fills the areas at the points the click's copies fall on (each found as the drawing has it there), the lasso the
// copies of its outline.
class FillTool : public CanvasTool
{
public:
    // Where the fill around `seed` may reach (the part of the canvas seen, in the cel's own place), and, if `withImage`,
    // the frame as shown there (premultiplied, the size of the area), for areas found in all layers.
    struct Shown
    {
        QRect area;
        QImage image;
    };
    using Frame = std::function<Shown(const QPoint &seed, bool withImage)>;
    // The transforms of the symmetry's copies of a fill begun at a point of the cel (none while it is off).
    using Copies = std::function<std::vector<QTransform>(const QPointF &celPos)>;

    FillTool(document::DocumentController *document, tools::ToolState *state, Frame frame,
             std::function<void(const QString &)> status, Copies copies = {});

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override { m_lasso.clear(); }
    bool isActive() const override { return !m_lasso.isEmpty(); } // the bucket is done on the press
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;

private:
    void fillAround(const QPointF &docPos);
    void fillLasso();
    // On a vector layer: `fill` (a shape of colour) put on the layer, above its other fills and under its lines, and the
    // layer drawn again; one undo step with `before`, the layer's pixels at beginPaint().
    void putFill(const document::DocumentController::PaintTarget &target, doc::VectorStroke fill,
                 const doc::TiledImage &before);
    // The main color at the fill opacity, premultiplied.
    doc::Pixel fillColor(const QColor &color) const; // the main color (a mask's: its gray) with the opacity

    QPolygonF m_lasso; // the outline being drawn
    document::DocumentController *m_document;
    tools::ToolState *m_state;
    Frame m_frame;
    std::function<void(const QString &)> m_status;
    Copies m_copies;
};

} // namespace mothion::canvas
