#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/Tile.h"

#include <QImage>
#include <QPolygonF>

#include <functional>

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// The paint bucket: a press fills the area around it with the main color,
// behind what is already drawn there, in the drawing at the current layer and frame (made if the frame is
// empty), as one undo step. The area is found in the current layer or in
// the frame as shown, by the tool's fill settings, and kept inside the
// selection if there is one.
//
// In lasso mode (FillSettings::Mode::Lasso) a drag draws an outline instead,
// shown open while drawn; the release fills its inside, over what is there.
class FillTool : public CanvasTool
{
public:
    // The frame as shown, for areas found in all layers.
    using Frame = std::function<const QImage &()>;

    FillTool(document::DocumentController *document, tools::ToolState *state, Frame frame,
             std::function<void(const QString &)> status);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override { m_lasso.clear(); }
    bool isActive() const override { return !m_lasso.isEmpty(); } // the bucket is done on the press
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;

private:
    void fillAround(const QPointF &docPos);
    void fillLasso();
    // The main color at the fill opacity, premultiplied.
    doc::Pixel fillColor() const;

    QPolygonF m_lasso; // the outline being drawn
    document::DocumentController *m_document;
    tools::ToolState *m_state;
    Frame m_frame;
    std::function<void(const QString &)> m_status;
};

} // namespace mothion::canvas
