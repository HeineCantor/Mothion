#pragma once

#include "canvas/CanvasTool.h"
#include "tools/ToolState.h"

#include <QImage>
#include <QLineF>
#include <QPainterPath>
#include <QPoint>
#include <QPolygonF>
#include <QRect>
#include <QRectF>

#include <functional>

namespace mothion::document {
class DocumentController;
}

namespace mothion::canvas {

// Where a shape lies, for a drag from `start` to `end` (document coordinates).
// A line has its two ends in `line`; a rectangle or an ellipse its box in `box`;
// a polygon or a curve, made by clicks, its points in `points`.
struct ShapeGeometry
{
    QLineF line;
    QRectF box;
    QPolygonF points;
};

// The outline of the shape (closed for all but the line and the curve). A
// curve is a smooth spline through its points.
QPainterPath shapePath(const tools::ShapeSettings &settings, const ShapeGeometry &geometry);

// Shift: a line in 15° steps, a square or a circle. Alt: `start` is the
// center, not a corner (or the middle of a line).
ShapeGeometry shapeGeometry(tools::ShapeSettings::Kind kind, const QPointF &start, const QPointF &end,
                            Qt::KeyboardModifiers modifiers);

// The shape drawn at full strength in the main color (the outline) and the
// secondary one (the fill under an outline), over transparent pixels, clipped
// to `clip`. The image is empty if nothing of it lies inside; `origin` is
// where its top left lies in the document.
struct RenderedShape
{
    QImage image; // Format_ARGB32_Premultiplied
    QPoint origin;
};
RenderedShape renderShape(const tools::ShapeSettings &settings, double thickness, const QColor &main,
                          const QColor &secondary, const ShapeGeometry &geometry, const QRect &clip);

// Draws a line, rectangle, rounded rectangle or ellipse by dragging, or a
// polygon or curve by clicking its points (the first point of a polygon, a
// double click or Enter finish it, Backspace takes the last point back, Esc
// drops it), in the current layer and
// frame (made if the frame is empty), as one undo step: the main color at the
// tool's opacity, kept inside the selection if there is one. While the main
// color is the transparent one, the shape erases instead. A click without a
// drag draws nothing.
class ShapeTool : public CanvasTool
{
public:
    // `changed` is called when the points of a polygon or curve in progress
    // change (the bar with OK and Cancel follows them).
    ShapeTool(document::DocumentController *document, tools::ToolState *state,
              std::function<void(const QString &)> status, std::function<void()> changed = {});

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_active; }
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;
    bool hover(const QPointF &docPos) override;
    bool hasPendingEdit() const override { return building(); }
    void commit() override;
    bool wantsKey(int key) const override;
    bool keyPress(int key) override;
    void setViewZoom(double zoom) override { m_zoom = zoom; }

    // The points placed so far, around (document coordinates; null when no
    // polygon or curve is in progress), and whether there are enough to finish.
    QRectF pendingBounds() const;
    bool canFinish() const;

private:
    bool building() const;
    void notify() const
    {
        if (m_changed)
            m_changed();
    }
    ShapeGeometry currentGeometry(bool withHover) const;
    RenderedShape render(const ShapeGeometry &geometry) const;
    void draw(const RenderedShape &shape);
    QPointF constrained(const QPointF &docPos, Qt::KeyboardModifiers modifiers) const;

    document::DocumentController *m_document;
    tools::ToolState *m_state;
    std::function<void(const QString &)> m_status;
    std::function<void()> m_changed;
    bool m_active = false;
    QPointF m_start;
    QPointF m_current;
    Qt::KeyboardModifiers m_modifiers;
    QPolygonF m_points; // a polygon or curve's points so far
    QPointF m_hover;    // where the next one would go
    double m_zoom = 1.0;
};

} // namespace mothion::canvas
