#pragma once

#include "canvas/DragThrottle.h"
#include "core/deform/CurveField.h"
#include "core/document/CelMotion.h"
#include "document/DocumentController.h"

#include <QPointF>
#include <QTransform>

#include <functional>
#include <optional>
#include <vector>

class QPainter;

namespace mothion::canvas {

class ViewTransform;

// The curve of a Curve or Envelope deformation cel on the canvas (see doc::CelProperties::deformer), while it is the
// current cel, whatever the tool: the curve (an Envelope's closed), a square on each node and, on either side of it, its
// handles. A deformation cel that follows a trajectory has its curve where the trajectory takes it.
//
// In rig mode (setRigMode()) the curve is at rest, and is made and shaped:
//  - a click on nothing adds a node: on from the chosen node if that is the last of its curve (each node added is chosen,
//    so clicks go on along one curve); with no such node chosen, or with Ctrl, it begins a curve of its own (a Curve
//    deformer holds several apart, as a Bones one several skeletons; an Envelope is one closed curve, always gone on);
//  - a node dragged moves; a handle dragged turns the node's tangent (the other handle stays opposite, as long as it was)
//    and sets that handle's length;
//  - on a Curve what comes after the node on its curve goes along, moved or turned with it (as bones do), unless Alt is
//    held; on an Envelope only that node changes;
//  - a click chooses a node (on nothing in a full Envelope: none), Delete takes the chosen one away;
//  - on a Curve the chosen node shows the zone round the curve there, with two handles: how far across it holds all of
//    the drawing, and how far beyond that it holds less and less of it.
// Each of these is one undo step.
//
// Out of rig mode the curve is as the frame poses it, and its nodes and handles are dragged in the same way to animate it.
// What a drag changes goes by the rules of the cel's keys (CelMotion): into the Deform key on the current frame, or, on a
// frame without one, as a pose that goes with the frame until it is made a key; a deformer with no key gets its first one
// from the first drag. One undo step.
class CurveGizmo
{
public:
    enum class Part { None, Node, In, Out, ZoneRadius, ZoneFade };
    struct Hit
    {
        int node = -1;
        Part part = Part::None;
    };

    explicit CurveGizmo(document::DocumentController *document);

    void setRigMode(bool on) { m_rig = on; }
    // Called when a drag drawn later changed what is shown.
    void setRepaint(std::function<void()> repaint) { m_throttle.setDrawnLater(std::move(repaint)); }
    // The current cel is a Curve or Envelope deformer and the camera is not the selected row.
    bool shown() const;
    bool editable() const;
    bool isDragging() const { return m_drag.has_value(); }

    Hit hitAt(const QPointF &docPos, const ViewTransform &view) const;
    bool press(const QPointF &docPos, const ViewTransform &view, Qt::KeyboardModifiers modifiers = {});
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers = {});
    void release(const QPointF &docPos);
    void cancel();
    bool hover(const QPointF &docPos, const ViewTransform &view);

    int chosenNode() const;
    bool removeChosenNode();

    void paint(QPainter &painter, const ViewTransform &view) const;
    // Where a part of a node is on the page as shown (the zone's handles: of the chosen node, in rig mode); for tests.
    std::optional<QPointF> partPoint(int node, Part part) const;

    static constexpr double kGrab = 8.0;

private:
    const doc::Cel *cel() const;
    bool closed() const;
    QTransform toPage() const;
    deform::Vec2 inCurve(const QPointF &docPos) const;
    // The nodes as shown: at rest in rig mode (as the drag changes them), else as the frame poses them.
    std::vector<deform::CurveNode> nodes() const;
    const doc::Deformer &deformerShown() const;

    struct Drag
    {
        int node = -1;
        Part part = Part::None;
        deform::Vec2 start, now;
        bool moved = false;
        bool local = false;       // only the node changes (an Envelope, or Alt on a Curve)
        std::vector<deform::CurveNode> startNodes;
        doc::Deformer rig;        // rig mode: the deformer as the drag makes it
        doc::CelMotion before;    // out of rig mode: the motion when the drag began
        std::vector<double> pose; // out of rig mode: the pose when the drag began
    };
    void apply(Drag &drag);

    document::DocumentController *m_document;
    bool m_rig = false;
    int m_chosen = -1;
    doc::CelId m_chosenCel = doc::kNoId;
    Hit m_hovered;
    std::optional<Drag> m_drag;
    DragThrottle m_throttle;
};

} // namespace mothion::canvas
