#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/VectorStroke.h"
#include "tools/ToolState.h"

#include <QPainterPath>
#include <QPolygonF>

#include <vector>

namespace mothion::document {
class DocumentController;
}

namespace mothion::canvas {

// The strokes of a vector layer that a selection drawn as `area` takes whole in `mode` (Enclosed or Touched; none for
// Area): by their lines (a fill by its outlines), in order. The eraser's strokes are no lines of their own: one wholly
// inside goes along with those that are taken.
std::vector<int> selectedStrokes(const std::vector<doc::VectorStroke> &strokes, const QPainterPath &area,
                                 tools::VectorSelectSettings::Mode mode);

// Draws a new selection: a rectangle (Shift: a square) or a free lasso
// outline. A click without a drag clears the selection. The lasso is shown
// open while drawn; on release it closes with a straight line back to the
// start. On a vector layer, with a mode other than Area (tools::VectorSelectSettings), what is selected is the whole of
// the lines the shape takes (see selectedStrokes()), not the shape.
class SelectionTool : public CanvasTool
{
public:
    enum class Shape { Rectangle, Lasso };

    SelectionTool(document::DocumentController *document, tools::ToolState *toolState, Shape shape);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_active; }
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;

private:
    QPainterPath shapeSoFar() const;

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
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
