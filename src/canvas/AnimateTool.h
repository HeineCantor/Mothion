#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/Cel.h"
#include "core/document/CelMotion.h"
#include "core/document/Ids.h"

#include <QRectF>
#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <vector>

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
// With several cels chosen together (Ctrl or Shift on their names) it moves them all as one piece: the box is round all of
// what they show, and what the drag does to it is done to each of them (a cel with no keys gets its first one).
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
    void invalidate() { m_bounds.clear(); }
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
    // The cels moved together, when several are chosen and show something here: the chosen ones that are free, on view, and
    // have pixels on the current frame. Fewer than two: the current cel alone.
    std::vector<doc::CelId> group() const;
    // The box round what a cel shows (measured once, until invalidate()), in its own coordinates; empty if nothing.
    QRect boundsOf(const doc::Cel &cel, const doc::Drawing &drawing) const;
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

    // One of the cels a drag moves: its motion when the drag began, and its pose and pivot then.
    struct Member
    {
        doc::CelId cel = doc::kNoId;
        doc::CelMotion before;
        doc::CelPose pose;
        QPointF pivot;
    };
    struct Drag
    {
        Grip grip = Grip::Move;
        doc::CelId cel = doc::kNoId;
        std::vector<Member> members; // with several cels chosen: all of them (this one among them)
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
    // The box round a drawing is measured once (it takes a pass over the pixels) until something invalidates it.
    mutable std::map<doc::DrawingId, QRect> m_bounds;
    std::optional<Drag> m_drag;
    struct Pending
    {
        doc::CelId cel = doc::kNoId; // the current cel when the edit began
        std::vector<Member> members; // every cel the edit moves, as it was when the first drag began
    };
    std::optional<Pending> m_pending;
};

} // namespace mothion::canvas
