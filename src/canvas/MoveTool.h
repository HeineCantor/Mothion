#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/TiledImage.h"

#include <QPainterPath>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace mothion::document {
class DocumentController;
}

namespace mothion::canvas {

// Moves the current layer by whole pixels, or every layer chosen in the
// Layers panel (all those of a chosen frame or cel), or only their selected
// part when there is a selection (which then moves along, live). One drag
// is one undo step, selection included; Shift keeps the move horizontal or
// vertical. The picture follows the pen once for each turn of the event loop, not for each pen sample (with
// every tablet sample kept, a big object would fall behind the pen).
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
    QCursor cursor() const override { return Qt::SizeAllCursor; }

private:
    void moveTo(QPoint offset);
    void flush(); // shows the last offset asked for

    document::DocumentController *m_document;
    std::function<void(const QString &)> m_status;

    struct Moving
    {
        doc::TiledImage *image = nullptr; // the layer being moved
        doc::TiledImage before;           // as it was at the press
        // With a selection only the selected pixels move: `floating` over `rest`.
        doc::TiledImage floating;
        doc::TiledImage rest;
    };
    struct Session
    {
        std::vector<Moving> layers;
        QRect content;   // area that moves
        QPointF start;   // press position
        QPoint offset;   // current move
        QPainterPath selection;
    };
    std::optional<Session> m_session;
    QPoint m_wanted; // where the pen is, as an offset; shown on the next turn of the event loop
    QTimer m_timer;
};

} // namespace mothion::canvas
