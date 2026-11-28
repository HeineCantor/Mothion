#include "canvas/MoveTool.h"

#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "canvas/VectorRender.h"
#include "document/DocumentController.h"

#include <algorithm>
#include <cmath>

namespace mothion::canvas {

MoveTool::MoveTool(document::DocumentController *document, std::function<void(const QString &)> status)
    : m_document(document)
    , m_status(std::move(status))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(0);
    QObject::connect(&m_timer, &QTimer::timeout, [this] { flush(); });
}

void MoveTool::flush()
{
    m_timer.stop();
    if (m_session)
        moveTo(m_wanted);
}

void MoveTool::press(const QPointF &docPos, Qt::KeyboardModifiers)
{
    QString reason;
    const auto targets = m_document->beginPaintLayers(&reason, true);
    if (targets.empty()) {
        if (reason.isEmpty())
            reason = QObject::tr("Nothing to move on this frame");
        m_status(reason);
        return;
    }
    Session s;
    s.selection = m_document->selection();
    const std::optional<SelectionMask> mask =
        s.selection.isEmpty() ? std::nullopt : std::optional(SelectionMask::fromPath(s.selection));
    for (const auto &target : targets) {
        Moving m;
        m.image = target.image;
        m.before = *target.image;
        if (target.vector) {
            m.target = target;
            if (const doc::Layer *layer = m_document->document().layer(target.cel, target.drawing, target.layer)) {
                m.allStrokes = layer->strokes;
                if (!mask) {
                    m.strokes = layer->strokes;
                } else { // cut along the outline (or the lines selected): the strokes inside move, the rest stays
                    auto parts = m_document->splitStrokes(target.cel, target.drawing, target.layer, layer->strokes, *mask);
                    m.strokes = std::move(parts.inside);
                    m.restStrokes = std::move(parts.outside);
                }
            }
        }
        if (mask) {
            m_document->splitPixels(target.cel, target.drawing, target.layer, m.allStrokes, m.before, *mask, m.floating,
                                    m.rest);
        } else {
            for (const doc::TileCoord &c : m.before.tileCoords())
                s.content |= tileRect(c);
        }
        s.layers.push_back(std::move(m));
    }
    if (mask)
        s.content = mask->bounds;
    s.start = docPos;
    m_wanted = {};
    m_session = std::move(s);
}

void MoveTool::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_session)
        return;
    QPoint offset(int(std::lround(docPos.x() - m_session->start.x())),
                  int(std::lround(docPos.y() - m_session->start.y())));
    if (modifiers & Qt::ShiftModifier) { // along the main axis only
        if (std::abs(offset.x()) >= std::abs(offset.y()))
            offset.setY(0);
        else
            offset.setX(0);
    }
    // Only the last position of a burst of samples is drawn.
    m_wanted = offset;
    if (!m_timer.isActive())
        m_timer.start();
}

void MoveTool::moveTo(QPoint offset)
{
    Session &s = *m_session;
    if (offset == s.offset)
        return;
    const QRect old = s.content.translated(s.offset);
    s.offset = offset;
    for (Moving &m : s.layers) {
        if (s.selection.isEmpty()) {
            *m.image = m.before.translated(offset.x(), offset.y());
        } else {
            doc::TiledImage moved = m.rest;
            compositeOver(moved, m.floating.translated(offset.x(), offset.y()));
            *m.image = std::move(moved);
        }
    }
    if (!s.selection.isEmpty())
        m_document->moveSelection(s.selection.translated(offset)); // the outline follows live
    m_document->notifyPainted(old | s.content.translated(offset));
}

void MoveTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_session)
        return;
    move(docPos, modifiers);
    flush();
    const Session s = std::move(*m_session);
    m_session.reset();
    // Pixels and selection in one undo step; nothing if it did not move.
    std::vector<doc::TiledImage> before;
    for (const Moving &m : s.layers) {
        before.push_back(m.before);
        if (!m.target || s.offset == QPoint())
            continue;
        // The strokes of a vector layer go with its pixels, in the same undo step.
        const QTransform shift = QTransform::fromTranslate(s.offset.x(), s.offset.y());
        std::vector<doc::VectorStroke> shifted;
        shifted.reserve(m.strokes.size());
        for (const doc::VectorStroke &stroke : m.strokes)
            shifted.push_back(transformedStroke(stroke, shift));
        const auto &t = *m.target;
        std::vector<doc::VectorStroke> moved =
            m_document->rejoined(t.cel, t.drawing, t.layer, m.allStrokes, m.restStrokes, shifted);
        if (m_document->picksOn(t.cel, t.drawing, t.layer) && m_document->pickedStrokes()->brushes) {
            // Whole lines, each in its place among the others: drawn again, not laid over.
            doc::TiledImage drawn;
            drawStrokes(drawn, moved, *m_document->pickedStrokes()->brushes, {});
            *m.image = std::move(drawn);
            m_document->notifyPainted(document::kWholeCanvas);
        }
        m_document->attachStrokeChange(t, m.allStrokes, std::move(moved));
    }
    m_document->endPaint(before, doc::CommandKind::Move);
}

void MoveTool::cancel()
{
    m_timer.stop();
    if (!m_session)
        return;
    Session s = std::move(*m_session);
    m_session.reset();
    for (const Moving &m : s.layers)
        *m.image = m.before;
    if (!s.selection.isEmpty())
        m_document->moveSelection(s.selection);
    m_document->notifyPainted(s.content.translated(s.offset) | s.content);
    m_document->cancelPaint();
}

} // namespace mothion::canvas
