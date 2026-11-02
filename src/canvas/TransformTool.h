#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/TiledImage.h"

#include <QImage>
#include <QPainterPath>
#include <QRect>
#include <QString>
#include <QTransform>

#include <functional>
#include <memory>
#include <optional>

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// Free transform of the selected pixels (all of the drawing at the current
// layer and frame when nothing is selected), in a box with eight handles:
// a corner scales (Shift keeps the proportions), a side scales one way,
// inside the box moves, outside rotates (Shift: 15° steps). The selection
// follows.
//
// The box comes up as soon as the tool is chosen and stays open across
// drags; Enter applies it as one undo step, Esc drops it, and either goes
// back to the tool used before. Another tool, undo or a frame or layer
// change applies it too.
class TransformTool : public CanvasTool
{
public:
    TransformTool(document::DocumentController *document, tools::ToolState *toolState,
                  std::function<void(const QString &)> status, std::function<void()> repaint);
    ~TransformTool() override;

    void activated() override;
    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_drag.has_value(); }
    bool hasPendingEdit() const override { return m_session != nullptr; }
    void commit() override;
    bool keyPress(int key) override;
    void setViewZoom(double zoom) override { m_zoom = zoom; }
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;
    Qt::CursorShape cursor() const override { return Qt::SizeAllCursor; }

    // The transform so far, from the pixels' place to where they are shown.
    QTransform transform() const { return m_session ? m_session->transform : QTransform(); }
    // The box being transformed, before the transform (document coordinates).
    QRect box() const { return m_session ? m_session->box : QRect(); }

private:
    enum class Grip { Move, Rotate, Corner, Side };
    struct Drag
    {
        Grip grip = Grip::Move;
        QTransform start;   // the transform at the press
        QPointF pressDoc;   // where the pen went down
        QPointF handle;     // the grabbed handle, in box coordinates
        QPointF anchor;     // the point that stays put, in box coordinates
        bool horizontal = false; // a side handle: which way it scales
    };

    bool start();
    void render();
    void finish(bool apply);
    // The eight handles in box coordinates: corners 0..3, then side middles.
    QPointF handlePoint(int index) const;

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    std::function<void()> m_repaint;
    double m_zoom = 1.0;

    struct Session
    {
        doc::TiledImage *image = nullptr; // the drawing being transformed
        doc::TiledImage before;           // as it was when the transform started
        doc::TiledImage rest;             // what stays put (outside the selection)
        QImage source;                    // the pixels transformed, over `box`
        QRect box;                        // tight bounds of those pixels
        QPainterPath selection;           // as it was, empty if none
        QTransform transform;
        QRect shown; // area last drawn by the transform
        bool changed = false;
    };
    std::unique_ptr<Session> m_session;
    std::optional<Drag> m_drag;
};

} // namespace mothion::canvas
