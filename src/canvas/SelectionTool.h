#pragma once

#include "canvas/CanvasTool.h"

#include <QPainterPath>
#include <QPolygonF>

namespace mothion::document {
class DocumentController;
}

namespace mothion::canvas {

// Draws a new selection: a rectangle (Shift: a square) or a free lasso
// outline. A click without a drag clears the selection. The lasso is shown
// open while drawn; on release it closes with a straight line back to the
// start.
class SelectionTool : public CanvasTool
{
public:
    enum class Shape { Rectangle, Lasso };

    SelectionTool(document::DocumentController *document, Shape shape);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_active; }
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;

private:
    QPainterPath shapeSoFar() const;

    document::DocumentController *m_document;
    Shape m_shape;
    bool m_active = false;
    QPointF m_start;
    QPointF m_current;
    bool m_square = false;
    QPolygonF m_lasso;
};

// The selection outline as "marching ants" (dashed black and white); phase 1
// swaps the black and white dashes.
void paintSelectionOutline(QPainter &painter, const ViewTransform &view, const QPainterPath &area, int phase = 0);

} // namespace mothion::canvas
