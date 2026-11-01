#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/TiledImage.h"

#include <QPainterPath>
#include <QPoint>
#include <QRect>
#include <QString>

#include <functional>
#include <optional>

namespace mothion::document {
class DocumentController;
}

namespace mothion::canvas {

// Moves the drawing at the current layer and frame by whole pixels, or only
// its selected part when there is a selection (which then moves along).
// One drag is one undo step; Shift keeps the move horizontal or vertical.
class MoveTool : public CanvasTool
{
public:
    // `status` shows why a move is refused (hidden or locked layer...).
    MoveTool(document::DocumentController *document, std::function<void(const QString &)> status);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_session.has_value(); }
    Qt::CursorShape cursor() const override { return Qt::SizeAllCursor; }

private:
    void moveTo(QPoint offset);

    document::DocumentController *m_document;
    std::function<void(const QString &)> m_status;

    struct Session
    {
        doc::TiledImage *image = nullptr; // the drawing being moved
        doc::TiledImage before;           // as it was at the press
        QRect content;                    // area that moves
        QPointF start;                    // press position
        QPoint offset;                    // current move
        // With a selection only the selected pixels move: `floating` over `rest`.
        QPainterPath selection;
        doc::TiledImage floating;
        doc::TiledImage rest;
    };
    std::optional<Session> m_session;
};

} // namespace mothion::canvas
