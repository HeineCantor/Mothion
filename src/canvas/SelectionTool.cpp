#include "canvas/SelectionTool.h"

#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace mothion::canvas {

namespace {
constexpr double kClickTolerance = 2.0; // document px: below this a drag is a click
}

std::vector<int> selectedStrokes(const std::vector<doc::VectorStroke> &strokes, const QPainterPath &area,
                                 tools::VectorSelectSettings::Mode mode)
{
    using Mode = tools::VectorSelectSettings::Mode;
    std::vector<int> lines, erasers;
    if (mode == Mode::Area)
        return lines;
    const QPolygonF outline = area.toFillPolygon();
    const QRectF bounds = outline.boundingRect();
    std::vector<QLineF> edges;
    for (qsizetype i = 0; i + 1 < outline.size(); ++i)
        edges.emplace_back(outline[i], outline[i + 1]);
    auto enclosed = [&](const doc::VectorStroke &stroke) {
        return std::all_of(stroke.points.begin(), stroke.points.end(),
                           [&](const brush::BrushInput &p) { return area.contains(QPointF(p.x, p.y)); });
    };
    auto touched = [&](const doc::VectorStroke &stroke) {
        QPointF last;
        for (std::size_t i = 0; i < stroke.points.size(); ++i) {
            const QPointF at(stroke.points[i].x, stroke.points[i].y);
            if (area.contains(at))
                return true;
            if (i > 0 && bounds.intersects(QRectF(last, at).normalized().adjusted(-0.5, -0.5, 0.5, 0.5))) {
                const QLineF piece(last, at);
                for (const QLineF &edge : edges)
                    if (piece.intersects(edge) == QLineF::BoundedIntersection)
                        return true;
            }
            last = at;
        }
        return false;
    };
    for (int i = 0; i < int(strokes.size()); ++i) {
        const doc::VectorStroke &stroke = strokes[std::size_t(i)];
        if (stroke.points.empty())
            continue;
        if (stroke.erase) {
            if (enclosed(stroke))
                erasers.push_back(i);
        } else if (mode == Mode::Enclosed ? enclosed(stroke) : touched(stroke)) {
            lines.push_back(i);
        }
    }
    if (!lines.empty()) {
        lines.insert(lines.end(), erasers.begin(), erasers.end());
        std::sort(lines.begin(), lines.end());
    }
    return lines;
}

SelectionTool::SelectionTool(document::DocumentController *document, tools::ToolState *toolState, Shape shape)
    : m_document(document)
    , m_toolState(toolState)
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
    else if (m_toolState->vectorSelect().mode != tools::VectorSelectSettings::Mode::Area
             && m_document->currentLayerIsVector()) {
        const doc::Layer *layer = m_document->document().layer(m_document->currentCel(), m_document->currentDrawingId(),
                                                               m_document->currentLayer());
        m_document->selectStrokes(layer ? selectedStrokes(layer->strokes, area, m_toolState->vectorSelect().mode)
                                        : std::vector<int>(),
                                  *m_toolState);
    } else {
        m_document->setSelection(area);
    }
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
    if (!m_active)
        return;
    if (m_shape == Shape::Lasso) { // open: the closing line only comes with the release
        QPainterPath open;
        open.addPolygon(m_lasso);
        paintSelectionOutline(painter, view, open);
    } else {
        paintSelectionOutline(painter, view, shapeSoFar());
    }
}

void paintSelectionOutline(QPainter &painter, const ViewTransform &view, const QPainterPath &area, int phase)
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
    dashes.setDashOffset(phase * 4); // a dash's length: black where white was
    painter.setPen(dashes);
    painter.drawPath(onScreen);
    painter.restore();
}

} // namespace mothion::canvas
