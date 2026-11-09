#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/CelMotion.h"
#include "core/document/Ids.h"

#include <QRectF>
#include <QString>

#include <functional>
#include <optional>

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// Moves, turns and scales the current cel as one piece on the current frame, and changes the cel's pose (a
// keyframe of the cel's motion, see doc::CelMotion): a box round what the cel shows on the frame, drawn as the pose has
// it, with a handle at each corner (scale, from the pivot), another above (turn, around the pivot; Shift: 15° steps),
// inside the box a drag moves it, and a cross at the pivot, where it turns and scales around. The cel's drawings are not
// touched. The box and the path of the pivot over the frames follow the keys.
//
// It is not a tool of the palette: the Transform and the Move tools are this (the Move tool only moves) when the keyframe
// current cel has keys (ToolState::keyframeMode()), and they work on the pixels otherwise. On a frame with a key the
// drag changes that key; on a frame with none it leaves a pose that goes when another frame is chosen, unless the Cel key
// button makes it a key.
class AnimateTool : public CanvasTool
{
public:
    AnimateTool(document::DocumentController *document, tools::ToolState *toolState,
                std::function<void(const QString &)> status, std::function<void()> repaint);

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_drag.has_value(); }
    // The drags are an open edit, as the Transform tool's: OK (Enter), or another frame, cel or tool, applies it as one undo
    // step (the key as it ended up), Cancel (Esc) puts the cel back as it was before the first drag.
    bool hasPendingEdit() const override { return m_pending.has_value(); }
    void commit() override;
    bool keyPress(int key) override;
    // The box of the open edit, as it is posed (null without one): where the OK / Cancel bar goes.
    QRectF pendingBounds() const;
    bool hover(const QPointF &docPos) override;
    void setViewZoom(double zoom) override { m_zoom = zoom; }
    // Only moving: a drag anywhere moves the cel (the Move tool's way), with no handles to turn or scale it.
    void setMoveOnly(bool on) { m_moveOnly = on; }
    // What the cel shows changed (pixels, frames, another document): the box is measured again.
    void invalidate() { m_boundsValid = false; }
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;
    QCursor cursor() const override;

private:
    enum class Grip { None, Move, Rotate, Scale };
    // What the tool works on now: the box round the cel's drawing on the current frame, its pivot and its pose.
    struct Shape
    {
        QRectF box;
        QPointF pivot;
        doc::CelPose pose;
        doc::Matrix2x3 matrix;
        QPointF pivotNow() const { return pivot + QPointF(pose.x, pose.y); }
    };
    std::optional<Shape> shape() const;
    QPointF mapped(const Shape &s, const QPointF &local) const;
    QPointF turnHandle(const Shape &s) const;
    Grip pick(const QPointF &docPos, const Shape &s) const;

    struct Drag
    {
        Grip grip = Grip::Move;
        doc::CelId cel = doc::kNoId;
        int frame = 0;
        doc::CelMotion before; // the motion when the pen went down
        doc::CelPose start;
        QPointF press;
        QPointF pivotNow;
        double startDistance = 1;
        double lastAngle = 0; // radians, of the pen around the pivot
        double turned = 0;    // degrees turned so far (past a half turn too)
    };

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    std::function<void()> m_repaint;
    double m_zoom = 1.0;
    Grip m_hover = Grip::None;
    bool m_moveOnly = false;
    // The box round a cel's drawing is measured once (it takes a pass over the pixels) until something invalidates it.
    mutable bool m_boundsValid = false;
    mutable doc::CelId m_boundsCel = doc::kNoId;
    mutable doc::DrawingId m_boundsDrawing = doc::kNoId;
    mutable QRect m_bounds;
    std::optional<Drag> m_drag;
    struct Pending
    {
        doc::CelId cel = doc::kNoId;
        doc::CelMotion before; // the motion when the first drag of the edit began
    };
    std::optional<Pending> m_pending;
};

} // namespace mothion::canvas
