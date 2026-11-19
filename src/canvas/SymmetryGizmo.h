#pragma once

#include "document/DocumentController.h"

#include <QElapsedTimer>
#include <QPointF>

#include <optional>

class QPainter;

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

class ViewTransform;

// The axes of the brushes' symmetry on the canvas (tools::SymmetrySettings, doc::Document::symmetryAxes()), while it
// is on: dashed guides through the center (see symmetryGuides()), a dot on the center and, where the axes turn, a knob
// along the first axis.
//
//  - the dot dragged moves the center; a double click on it puts the axes back in the middle of the page, upright;
//  - the knob dragged turns the axes round the center (Shift: 15° steps);
//  - Ctrl+drag anywhere (with a brush or the eraser) brings the center to the pen and moves it.
//
// The axes are saved with the project, but are not an undo step.
class SymmetryGizmo
{
public:
    enum class Handle { None, Center, Turn };

    SymmetryGizmo(document::DocumentController *document, tools::ToolState *toolState);

    bool shown() const;
    bool isDragging() const { return m_drag.has_value(); }
    Handle hovered() const { return m_hovered; }
    Handle handleAt(const QPointF &docPos, const ViewTransform &view) const;
    // A point of the document that grabs `handle`; for tests.
    std::optional<QPointF> handlePoint(Handle handle, const ViewTransform &view) const;

    // A press on a handle starts a drag (true); with `anywhere`, a press elsewhere brings the center there and drags it.
    bool press(const QPointF &docPos, const ViewTransform &view, bool anywhere = false);
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers = {});
    void release(const QPointF &docPos);
    // A drag cut short: the axes as they were.
    void cancel();
    // The handle under the pointer; true if that changed (repaint).
    bool hover(const QPointF &docPos, const ViewTransform &view);

    void paint(QPainter &painter, const ViewTransform &view) const;

    // Sizes on screen, px.
    static constexpr double kDot = 6.0;      // the center's radius
    static constexpr double kKnob = 6.0;     // the knob's radius
    static constexpr double kKnobAway = 70.0; // from the center
    static constexpr double kGrab = 9.0;     // how near a handle grabs it

private:
    QPointF knob(const ViewTransform &view) const; // view coordinates

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    Handle m_hovered = Handle::None;
    struct Drag
    {
        Handle handle = Handle::None;
        QPointF center; // as at the press (page coordinates)
        double angle = 0;
        bool placed = false;
        bool grabbed = false; // the handle itself was pressed (not the center brought to the pen)
        QPointF press;
    };
    std::optional<Drag> m_drag;
    QElapsedTimer m_lastCenterClick; // for the double click on the center
};

} // namespace mothion::canvas
