#include "canvas/MoveTool.h"

#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "document/DocumentController.h"

#include <cmath>

namespace mothion::canvas {

MoveTool::MoveTool(document::DocumentController *document, std::function<void(const QString &)> status)
    : m_document(document)
    , m_status(std::move(status))
{
}

void MoveTool::press(const QPointF &docPos, Qt::KeyboardModifiers)
{
    QString reason;
    const auto target = m_document->beginPaint(false, &reason);
    if (!target) {
        if (reason.isEmpty())
            reason = QObject::tr("Nothing to move on this frame");
        m_status(reason);
        return;
    }
    Session s;
    s.image = target->image;
    s.before = *target->image;
    if (m_document->hasSelection()) {
        s.selection = m_document->selection();
        const SelectionMask mask = SelectionMask::fromPath(s.selection);
        splitByMask(s.before, mask, s.floating, s.rest);
        s.content = mask.bounds;
    } else {
        for (const doc::TileCoord &c : s.before.tileCoords())
            s.content |= tileRect(c);
    }
    s.start = docPos;
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
    moveTo(offset);
}

void MoveTool::moveTo(QPoint offset)
{
    Session &s = *m_session;
    if (offset == s.offset)
        return;
    const QRect old = s.content.translated(s.offset);
    s.offset = offset;
    if (s.selection.isEmpty()) {
        *s.image = s.before.translated(offset.x(), offset.y());
    } else {
        doc::TiledImage moved = s.rest;
        compositeOver(moved, s.floating.translated(offset.x(), offset.y()));
        *s.image = std::move(moved);
        m_document->setSelection(s.selection.translated(offset)); // the outline follows live
    }
    m_document->notifyPainted(old | s.content.translated(offset));
}

void MoveTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_session)
        return;
    move(docPos, modifiers);
    const Session s = std::move(*m_session);
    m_session.reset();
    // Pixels and selection in one undo step; nothing if it did not move.
    m_document->endPaint(s.before, doc::CommandKind::Move);
}

void MoveTool::cancel()
{
    if (!m_session)
        return;
    Session s = std::move(*m_session);
    m_session.reset();
    *s.image = s.before;
    if (!s.selection.isEmpty())
        m_document->setSelection(s.selection);
    m_document->notifyPainted(s.content.translated(s.offset) | s.content);
    m_document->cancelPaint();
}

} // namespace mothion::canvas
