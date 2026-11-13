#pragma once

#include "core/document/CelMotion.h"
#include "core/document/Ids.h"
#include "document/DocumentController.h"

#include <QPointF>
#include <QTransform>

#include <optional>

class QPainter;

namespace mothion::canvas {

class ViewTransform;

// A trajectory cel on the canvas (see doc::CelProperties::trajectory): while it is the current cel, a dashed circle with a
// line from its middle to an arrow beyond the edge, whatever the tool:
//
//  - the middle (its dot) is taken to move the trajectory;
//  - the arrow turns it, round the middle;
//  - the edge of the circle scales it (the circle grows and shrinks with the scale).
//
// The keys of the trajectory's path (the dots on it, see paintPath()) are dragged along the page to move that key's position:
// the path bends to follow. One undo step, whatever the tool, and also while a cel that follows the trajectory is the current one.
//
// The middle is where the pivot is moved by the pose; the arrow points the way the rotation says (0 degrees to the right).
// A trajectory that follows another is drawn where it is, moved by its parents; what is dragged changes its own pose, which
// is on top of what the parents do (a faint line runs to the parent's middle). An inactive trajectory is drawn faint and
// cannot be dragged: its own pose does nothing.
// What a drag changes goes by the rules of the cel's keys (CelMotion): into the key of the channel on the current frame, or,
// on a frame without one, as a pose that goes with the frame until it is made a key; a trajectory with no key at all gets its
// first one from the first drag. The drag is one undo step.
class TrajectoryGizmo
{
public:
    enum class Handle { None, Move, Rotate, Scale, Key }; // Key: a key's dot on the path (see paintPath())

    explicit TrajectoryGizmo(document::DocumentController *document);

    // The current cel is a trajectory, it is active and not locked, and the camera is not the selected row.
    bool editable() const;
    // It shows (as editable() but a locked trajectory is shown too).
    bool shown() const;
    bool isDragging() const { return m_drag.has_value(); }
    Handle hovered() const { return m_hovered; }

    Handle handleAt(const QPointF &docPos, const ViewTransform &view) const;
    // The path's keys can be dragged: the position keys of the trajectory whose path is shown, if it is not locked.
    bool pathEditable() const;
    // The frame of the position key whose dot is under `docPos` (within kGrab on screen), if any.
    std::optional<int> keyAt(const QPointF &docPos, const ViewTransform &view) const;
    // The frame of the key being dragged or under the pointer (-1 for none).
    int keyFrame() const { return m_drag && m_drag->handle == Handle::Key ? m_drag->frame : m_hoverKey; }
    // A point of the document that grabs `handle`; for tests.
    std::optional<QPointF> handlePoint(Handle handle, const ViewTransform &view) const;

    // A press on a handle starts a drag (true); elsewhere nothing (false).
    bool press(const QPointF &docPos, const ViewTransform &view);
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers = {});
    void release(const QPointF &docPos);
    // A drag cut short: the trajectory as it was.
    void cancel();
    bool hover(const QPointF &docPos, const ViewTransform &view);

    void paint(QPainter &painter, const ViewTransform &view) const;
    // The whole path of the trajectory on the page, from its first key to its last: a line through where its middle is on
    // each frame, a dot on each frame and a bigger one on each key. Shown while the current cel is a trajectory, or follows
    // one (the path is that trajectory's). Painted under the circle.
    void paintPath(QPainter &painter, const ViewTransform &view) const;
    // The trajectory whose path is shown: the current cel if it is one, or the one it follows; nullptr for none (or the
    // camera is selected, trajectory mode is off, or the trajectory is inactive).
    const doc::Cel *pathTrajectory() const;
    // Trajectory mode (ToolState::trajectoryMode()): the path is shown, and its keys dragged, only while it is on.
    void setPathVisible(bool on) { m_pathVisible = on; }
    bool pathShown() const { return pathTrajectory() != nullptr; }
    // Where the trajectory's middle is on `frame` (document coordinates).
    QPointF pathPoint(const doc::Cel &trajectory, int frame) const;
    // The first and last frame of the path: the keys of the trajectory and of the ones it follows. False if none has a key.
    bool pathRange(const doc::Cel &trajectory, int *first, int *last) const;

    // Sizes on screen, px.
    static constexpr double kRadius = 60.0;   // the circle at scale 1
    static constexpr double kMinRadius = 14.0;
    static constexpr double kArrowAway = 28.0; // the arrow's tip beyond the edge
    static constexpr double kGrab = 9.0;       // how close to a handle on screen grabs it

private:
    struct Shape
    {
        QPointF center;  // view coordinates
        QPointF centerDoc;
        double radius = 0; // px
        QPointF tip;       // view coordinates
        QPointF direction; // the arrow's way on screen, a unit vector
        doc::CelPose pose; // its own (none if it is inactive)
        doc::Matrix2x3 parents; // what its parents do
        std::optional<QPointF> parentCenter; // view coordinates, of the parent's circle, if it follows one
    };
    std::optional<Shape> shape(const ViewTransform &view) const;
    Handle circleHandleAt(const QPointF &docPos, const ViewTransform &view) const;

    document::DocumentController *m_document;
    Handle m_hovered = Handle::None;
    int m_hoverKey = -1;
    bool m_pathVisible = false;
    struct Drag
    {
        Handle handle = Handle::None;
        doc::CelId cel = doc::kNoId;
        int frame = 0;
        doc::CelMotion before; // the motion at the press
        doc::CelPose start;    // the pose at the press
        QPointF press;         // document coordinates
        QPointF centerDoc;
        QTransform toView;     // as at the press
        doc::Matrix2x3 parents;        // what the parents did then
        doc::Matrix2x3 parentsBack;    // and the way back
        double turnWay = 1;            // -1 when the parents turn the picture over
        double pressDistance = 1; // the pen's screen distance from the middle at the press
        double lastAngle = 0;     // radians, of the pen around the middle
        double turned = 0;        // degrees so far
    };
    std::optional<Drag> m_drag;
};

} // namespace mothion::canvas
