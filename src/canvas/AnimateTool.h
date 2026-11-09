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

// The Transform and Move tools on a cel that has keys: they move, turn and scale it as one piece on the current frame, the
// Transform tool as it does the pixels: a box round what the cel shows (drawn as the pose has it) with eight handles, a
// corner scales (Shift keeps the proportions), a side scales one way, inside the box moves, outside turns (Shift: 15°
// steps); the box is there as soon as the tool is chosen, Enter (or OK) applies and Esc (or Cancel) drops, and either goes
// back to the tool used before. The Move tool only moves, by a drag anywhere. The cel's drawings are not touched.
//
// What a drag changes goes into the keys of those channels (position, rotation, scale) on the frame: on a frame with a
// key of a channel its key changes, on one with none the value goes when another frame is chosen, unless the Cel key
// button makes it a key. The path of the pivot over the frames follows the keys.
//
// It is not a tool of the palette: the Transform and the Move tools are this when the current cel has keys
// (ToolState::keyframeMode()), and work on the pixels otherwise.
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
    // The box as it is posed, while it shows (null otherwise): where the OK / Cancel bar goes.
    QRectF shownBounds() const;
    bool hover(const QPointF &docPos) override;
    void setViewZoom(double zoom) override { m_zoom = zoom; }
    // Only moving: a drag anywhere moves the cel (the Move tool's way), with no handles to turn or scale it.
    void setMoveOnly(bool on) { m_moveOnly = on; }
    // What the cel shows changed (pixels, frames, another document): the box is measured again.
    void invalidate() { m_boundsValid = false; }
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;
    QCursor cursor() const override;

private:
    enum class Grip { None, Move, Rotate, Corner, Side };
    // What the tool works on now: the box round the cel's drawing on the current frame, its pivot and its pose.
    struct Shape
    {
        QRectF box;
        QPointF pivot;
        doc::CelPose pose;
        doc::Matrix2x3 matrix;
    };
    std::optional<Shape> shape() const;
    QPointF mapped(const Shape &s, const QPointF &local) const;
    QPointF handlePoint(const Shape &s, int index) const; // in the cel's own coordinates: corners 0..3, then side middles
    struct Pick
    {
        Grip grip = Grip::None;
        int handle = -1; // 0..7 for a corner or a side
    };
    Pick pick(const QPointF &docPos, const Shape &s) const;
    bool showsBox() const; // the Transform tool's box is always there; the Move tool's only while an edit is open

    struct Drag
    {
        Grip grip = Grip::Move;
        doc::CelId cel = doc::kNoId;
        int frame = 0;
        doc::CelMotion before; // the motion when the pen went down
        Shape start;           // the shape then
        QPointF press;
        QPointF handle;        // the grabbed handle, in the cel's coordinates
        QPointF anchor;        // the point that stays put (the opposite corner or side)
        bool horizontal = false; // a side handle: which way it scales
        double lastAngle = 0;  // radians, of the pen around the middle of the box
        double turned = 0;     // degrees turned so far (past a half turn too)
    };

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    std::function<void()> m_repaint;
    double m_zoom = 1.0;
    Pick m_hover;
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
