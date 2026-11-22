#pragma once

#include "core/deform/BoneField.h"
#include "core/document/CelMotion.h"
#include "document/DocumentController.h"

#include "canvas/DragThrottle.h"

#include <QPointF>
#include <QTransform>

#include <functional>
#include <optional>
#include <vector>

class QPainter;

namespace mothion::canvas {

class ViewTransform;

// The bones of a Bones deformation cel on the canvas (see doc::CelProperties::deformer), while it is the current cel,
// whatever the tool. Each bone is drawn from its start to its end, with a dot on each joint; the chosen one shows the zone
// it holds. A deformation cel that follows a trajectory has its bones where the trajectory takes them.
//
// In rig mode (setRigMode()) the bones are at rest, and are made and placed:
//  - a drag on nothing draws a new bone, a root; a drag from the end of a bone with nothing hanging from it draws the next
//    one, hanging from it (Ctrl+drag from any bone's end: another one hanging from the same end);
//  - a joint dragged moves (the bones that meet there keep their other ends; Alt+drag on the end of the last bone moves it
//    instead of drawing the next one); a bone's middle dragged moves it with all that hangs from it;
//  - a click chooses a bone (on nothing: none), Delete takes the chosen one away with what hangs from it;
//  - the chosen bone shows its zone, with two handles across its middle: the inner one sets how far it holds all of the
//    drawing, the outer one how far beyond that it holds less and less of it.
// Each of these is one undo step.
//
// Out of rig mode the bones are as the frame poses them, and are animated: a bone dragged turns round its start (what
// hangs from it turns with it), with Alt it also grows or shrinks to the pointer; a root's start dragged moves it. What a
// drag changes goes by the rules of the cel's keys (CelMotion): into the Deform key on the current frame, or, on a frame
// without one, as a pose that goes with the frame until it is made a key; a deformer with no key gets its first one from the
// first drag. One undo step.
class BoneGizmo
{
public:
    enum class Part { None, Body, Origin, End, ZoneRadius, ZoneFade };
    struct Hit
    {
        int bone = -1;
        Part part = Part::None;
    };

    explicit BoneGizmo(document::DocumentController *document);
    // Called when a drag drawn later (see move()) changed what is shown.
    void setRepaint(std::function<void()> repaint) { m_throttle.setDrawnLater(std::move(repaint)); }

    void setRigMode(bool on) { m_rig = on; }
    bool rigMode() const { return m_rig; }
    // The current cel is a Bones deformer and the camera is not the selected row.
    bool shown() const;
    // Shown, and not locked.
    bool editable() const;
    bool isDragging() const { return m_drag.has_value(); }

    // The part of a bone under `docPos` (joints first), within kGrab on screen.
    Hit hitAt(const QPointF &docPos, const ViewTransform &view) const;
    // A press that starts a drag or chooses a bone (true); false when not editable.
    bool press(const QPointF &docPos, const ViewTransform &view, Qt::KeyboardModifiers modifiers = {});
    // Out of rig mode a move bends the drawings again, which takes time: a pen sends far more samples than that, so the
    // drag is drawn again only once the last drawing's time has passed (the last place always is, later if need be).
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers = {});
    void release(const QPointF &docPos);
    // A drag cut short: the bones as they were.
    void cancel();
    bool hover(const QPointF &docPos, const ViewTransform &view);
    Hit hovered() const { return m_hovered; }

    int chosenBone() const;
    void setChosenBone(int bone) { m_chosen = bone; }
    // In rig mode: takes the chosen bone away (and what hangs from it); false if none is chosen.
    bool removeChosenBone();

    void paint(QPainter &painter, const ViewTransform &view) const;
    // Where a part of a bone is on the page as shown (its middle for Body; the zone's handles of the chosen bone in rig
    // mode); for tests.
    std::optional<QPointF> partPoint(int bone, Part part) const;

    static constexpr double kGrab = 8.0; // how close on screen grabs a joint or a bone

private:
    const doc::Cel *cel() const;
    // The bones' place on the page as shown: what the trajectory the deformer follows does.
    QTransform toPage() const;
    // The bones as shown: at rest in rig mode (as the drag changes them), else as the frame poses them.
    std::vector<deform::BoneFrame> frames() const;
    deform::Vec2 inBones(const QPointF &docPos) const;

    struct Drag
    {
        enum class Kind { NewBone, MoveEnd, MoveOrigin, MoveBody, Zone, Turn, Translate } kind = Kind::Turn;
        Part part = Part::None;   // for Zone: which handle
        int bone = -1;            // the bone dragged (for NewBone: the parent, -1 for a root)
        deform::Vec2 start, now;  // in the bones' place
        bool moved = false;
        bool stretch = false;     // Turn with Alt: the length follows too
        doc::Deformer rig;        // rig mode: the bones as the drag makes them
        doc::CelMotion before;    // out of rig mode: the motion when the drag began
        std::vector<double> pose; // out of rig mode: the pose when the drag began
        std::vector<deform::BoneFrame> startFrames;
    };
    void apply(Drag &drag);

    document::DocumentController *m_document;
    bool m_rig = false;
    int m_chosen = -1;
    doc::CelId m_chosenCel = doc::kNoId; // the cel the chosen bone is of
    Hit m_hovered;
    std::optional<Drag> m_drag;
    DragThrottle m_throttle;
};

} // namespace mothion::canvas
