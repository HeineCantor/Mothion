#pragma once

#include "core/document/Camera.h"
#include "document/DocumentController.h"

#include <QPointF>

#include <optional>

class QPainter;

namespace mothion::canvas {

class ViewTransform;

// The camera on the canvas: its frame (a rectangle, red) over the sheet, where the camera looks at the current
// frame. Seen faintly while another row of the Layers panel is selected; with the camera selected it is strong,
// the sheet around it is dimmed, and it has handles, whatever the tool:
//
//  - dragging inside the frame moves the camera;
//  - the squares at its corners zoom it (the frame grows or shrinks around its middle);
//  - the round knob above its top edge turns it.
//
// Whatever is changed at the current frame becomes the camera's key there (a key is made if there is none), as
// the drag goes; the drag is one undo step. An invisible camera (off) is drawn dashed, and does nothing.
class CameraGizmo
{
public:
    enum class Handle { None, Move, Scale, Rotate };

    explicit CameraGizmo(document::DocumentController *document);

    // The camera is there to be moved: it exists, is on and is the selected row.
    bool editable() const;
    bool isDragging() const { return m_drag.has_value(); }
    Handle hovered() const { return m_hovered; }

    Handle handleAt(const QPointF &docPos, const ViewTransform &view) const;
    // A point of the document that grabs `handle` (a corner, the knob, the middle of the frame); for tests.
    std::optional<QPointF> handlePoint(Handle handle, const ViewTransform &view) const;

    // A press on the frame or a handle starts a drag (true); elsewhere nothing (false).
    bool press(const QPointF &docPos, const ViewTransform &view);
    void move(const QPointF &docPos);
    void release(const QPointF &docPos);
    // A drag cut short: the camera as it was.
    void cancel();
    // The handle under the pointer; true if that changed (repaint).
    bool hover(const QPointF &docPos, const ViewTransform &view);

    void paint(QPainter &painter, const ViewTransform &view) const;

    // Sizes on screen, px.
    static constexpr double kCorner = 7.0;  // half the side of a corner square
    static constexpr double kKnob = 8.0;    // the radius of the knob
    static constexpr double kKnobAway = 34.0; // how far above the top edge

private:
    struct Shape
    {
        doc::CameraPose pose;
        QPointF corners[4]; // view coordinates, top left first
        QPointF center;
        QPointF knob;
    };
    std::optional<Shape> shape(const ViewTransform &view) const;
    void show(const doc::CameraPose &pose);

    document::DocumentController *m_document;
    Handle m_hovered = Handle::None;
    struct Drag
    {
        Handle handle = Handle::None;
        std::optional<doc::Camera> before; // the camera as it was at the press
        int frame = 0;
        doc::CameraPose pose; // as it was at the press
        QPointF press;        // sheet coordinates
        double lastAngle = 0; // a rotation: where the pen was, degrees around the frame's middle
        double turned = 0;    // and how far it has gone round since the press
        bool moved = false;
    };
    std::optional<Drag> m_drag;
};

} // namespace mothion::canvas
