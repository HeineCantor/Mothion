#pragma once

#include "canvas/DragThrottle.h"
#include "core/document/Document.h"
#include "core/rig3d/Ik.h"

#include <QElapsedTimer>
#include <QPointF>
#include <QTransform>

#include <functional>
#include <optional>
#include <vector>

class QPainter;

namespace mothion::document {
class DocumentController;
}

namespace mothion::canvas {

class ViewTransform;

// The bones of a 3D model cel on the canvas (see doc::CelProperties::model3d), while it is the current cel, whatever the
// tool: each joint a dot where the camera of the frame's pose sees it, a line to its parent's. They are posed by
// dragging, and what a drag changes goes into the cel's Deform key on the frame by the rules of its keys (CelMotion), the
// first key coming from the first drag; one undo step:
//  - a joint dragged: its parent turns to point it at the pointer (in the plane across the view through the joint), and
//    the bones above that follow (rig3d::JointSettings::follow) take a share of the turn; a root's joint: the model moves;
//  - Shift+drag on a joint: inverse kinematics, the chain from it up to the root (or an anchor) solved by CCD within the
//    bones' limits (rig3d::solveIk);
//  - Ctrl+drag on a joint: its bone twists round itself, a degree each step of the pointer (one way or the other as it
//    goes right/down or left/up);
//  - a drag on nothing (with a tool that does not move cels: not Transform or Move): the camera turns round the model;
//    Alt+drag: it moves across the view; Ctrl+drag: it comes nearer or goes away.
// A click chooses a joint; a double click on one asks for its settings (setEditJoint()). In rig mode the chosen joint
// shows its limit frame: the bone's own axis (red) and the two its bends go toward (green, blue).
class ModelGizmo
{
public:
    explicit ModelGizmo(document::DocumentController *document);
    void setRepaint(std::function<void()> repaint) { m_throttle.setDrawnLater(std::move(repaint)); }
    // What a double click on a joint does: (cel, node).
    void setEditJoint(std::function<void(doc::CelId, int)> edit) { m_editJoint = std::move(edit); }
    void setRigMode(bool on) { m_rig = on; }
    // Whether a drag on nothing turns the camera (false while a tool that moves cels is at work).
    void setOrbitOnEmpty(bool on) { m_orbitOnEmpty = on; }

    bool shown() const;
    bool editable() const;
    bool isDragging() const { return m_drag.has_value(); }

    // The joint (a node of the model) under `docPos`, within kGrab on screen; -1 for none.
    int jointAt(const QPointF &docPos, const ViewTransform &view) const;
    bool press(const QPointF &docPos, const ViewTransform &view, Qt::KeyboardModifiers modifiers = {});
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers = {});
    void release(const QPointF &docPos);
    void cancel();
    bool hover(const QPointF &docPos, const ViewTransform &view);

    int chosenJoint() const;
    void setChosenJoint(int node) { m_chosen = node; }
    // Where a joint is on the page as shown; for tests.
    std::optional<QPointF> jointPoint(int node) const;

    void paint(QPainter &painter, const ViewTransform &view) const;

    static constexpr double kGrab = 8.0;

private:
    const doc::Cel *cel() const;
    // The page as the model is drawn on it to the document as shown: the cel's own pose and what it follows.
    QTransform toDocument() const;
    // The pose on the current frame (or the one the drag is making).
    rig3d::Pose currentPose() const;
    rig3d::Projection projection(const rig3d::Pose &pose) const;
    QPointF onPage(const QPointF &docPos) const;
    // The joints in the document as shown, by node (those that are no joint: nothing).
    std::vector<std::optional<QPointF>> jointPoints(const rig3d::Pose &pose) const;

    struct Drag
    {
        enum class Kind { Aim, Translate, Ik, Twist, Orbit, Pan, Dolly } kind = Kind::Aim;
        int node = -1;
        QPointF startPage, nowPage;   // on the page as the model is drawn
        QPointF lastView;             // the pointer, in view pixels (the twist's steps)
        QPointF startView;
        bool moved = false;
        doc::CelMotion before;        // the motion when the drag began
        rig3d::Pose startPose;
        rig3d::Pose pose;             // as the drag makes it
        double twistSteps = 0;
    };
    void apply(Drag &drag);
    void publish(const rig3d::Pose &pose);

    document::DocumentController *m_document;
    bool m_rig = false;
    bool m_orbitOnEmpty = true;
    int m_chosen = -1;
    int m_hovered = -1;
    doc::CelId m_chosenCel = doc::kNoId;
    std::optional<Drag> m_drag;
    DragThrottle m_throttle;
    std::function<void(doc::CelId, int)> m_editJoint;
    QElapsedTimer m_lastClick;
    int m_lastClickJoint = -1;
    const ViewTransform *m_view = nullptr; // the view of the last press (the twist's steps are in its pixels)
};

} // namespace mothion::canvas
