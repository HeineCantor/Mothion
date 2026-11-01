#include "canvas/SelectionTool.h"

#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"

#include <QPainter>

#include <cmath>

namespace mothion::canvas {

namespace {
constexpr double kClickTolerance = 2.0; // document px: below this a drag is a click
}

SelectionTool::SelectionTool(document::DocumentController *document, Shape shape)
    : m_document(document)
    , m_shape(shape)
{
}

void SelectionTool::press(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    m_active = true;
    m_start = m_current = docPos;
    m_square = modifiers & Qt::ShiftModifier;
    m_lasso = QPolygonF{docPos};
}

void SelectionTool::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_active)
        return;
    m_current = docPos;
    m_square = modifiers & Qt::ShiftModifier;
    if (m_shape == Shape::Lasso && QLineF(m_lasso.last(), docPos).length() >= 1.0)
        m_lasso << docPos;
}

void SelectionTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_active)
        return;
    move(docPos, modifiers);
    m_active = false;
    const QPainterPath area = shapeSoFar();
    const QRectF box = area.boundingRect();
    if (box.width() < kClickTolerance && box.height() < kClickTolerance)
        m_document->clearSelection(); // a click deselects
    else
        m_document->setSelection(area);
}

void SelectionTool::cancel()
{
    m_active = false;
}

QPainterPath SelectionTool::shapeSoFar() const
{
    QPainterPath path;
    if (m_shape == Shape::Rectangle) {
        QRectF r = QRectF(m_start, m_current).normalized();
        if (m_square) {
            const double side = std::max(r.width(), r.height());
            r = QRectF(m_start, QSizeF(m_current.x() < m_start.x() ? -side : side,
                                       m_current.y() < m_start.y() ? -side : side))
                    .normalized();
        }
        // Whole pixels, so a rectangle selects pixels fully or not at all.
        path.addRect(QRectF(QPointF(std::round(r.left()), std::round(r.top())),
                            QPointF(std::round(r.right()), std::round(r.bottom()))));
    } else if (m_lasso.size() >= 3) {
        path.addPolygon(m_lasso);
        path.closeSubpath();
    }
    return path;
}

void SelectionTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    if (m_active)
        paintSelectionOutline(painter, view, shapeSoFar());
}

void paintSelectionOutline(QPainter &painter, const ViewTransform &view, const QPainterPath &area)
{
    if (area.isEmpty())
        return;
    const QPainterPath onScreen = view.documentToView().map(area);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 1));
    painter.drawPath(onScreen);
    QPen dashes(Qt::black, 1, Qt::CustomDashLine);
    dashes.setDashPattern({4, 4});
    painter.setPen(dashes);
    painter.drawPath(onScreen);
    painter.restore();
}

} // namespace mothion::canvas
