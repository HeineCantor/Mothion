#pragma once

#include <QPointF>
#include <Qt>

class QPainter;

namespace mothion::canvas {

class ViewTransform;

// A tool that edits the document by pressing and dragging on the canvas
// (move, selection, puppet warp...), as opposed to painting strokes.
//
// CanvasView hands it the pen in document coordinates when it is the active
// tool and no navigation gesture takes the press. Changes go through the
// DocumentController, as an edit that is recorded when the tool decides
// (usually on release) and cancelled by cancel(), which the canvas calls
// when the controller interrupts the edit (undo, frame or layer change).
class CanvasTool
{
public:
    virtual ~CanvasTool() = default;

    virtual void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) = 0;
    virtual void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) = 0;
    virtual void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) = 0;
    // Puts things back as they were before the press.
    virtual void cancel() = 0;
    // Between press and release.
    virtual bool isActive() const = 0;

    // The tool was just chosen (a tool that shows something at once).
    virtual void activated() {}

    // An edit that lasts beyond one drag (a puppet warp with its pins): the
    // canvas commits it when the tool changes or the controller interrupts.
    virtual bool hasPendingEdit() const { return false; }
    virtual void commit() {}
    // Keys while the tool is active; true if handled.
    virtual bool keyPress(int key)
    {
        (void)key;
        return false;
    }
    // The pen or mouse moving over the canvas with nothing pressed; true if
    // the overlay changes with it.
    virtual bool hover(const QPointF &docPos)
    {
        (void)docPos;
        return false;
    }
    // Wheel turned over the canvas by `steps` notches (+ away from the user);
    // true if the tool took it (the view does not zoom then).
    virtual bool wheel(double steps, Qt::KeyboardModifiers modifiers)
    {
        (void)steps;
        (void)modifiers;
        return false;
    }
    // View zoom, for handles that should be grabbed at a fixed screen size.
    virtual void setViewZoom(double zoom) { (void)zoom; }

    // Handles and guides drawn over the canvas, in view coordinates.
    virtual void paintOverlay(QPainter &painter, const ViewTransform &view) const
    {
        (void)painter;
        (void)view;
    }
    virtual Qt::CursorShape cursor() const { return Qt::CrossCursor; }
};

} // namespace mothion::canvas
