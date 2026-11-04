#pragma once

#include "core/document/Drawing.h"
#include "document/DocumentController.h"

#include <QPointF>

#include <array>
#include <optional>
#include <vector>

class QMenu;
class QPainter;
class QWidget;

namespace mothion::canvas {

class ViewTransform;

// The handles of a perspective layer, at the center of its grid on the
// canvas, shown while that layer is the current one (whatever the tool):
//
//  - three rings (red, green, blue) turn the plane around its own x, y and z
//    axes (along its grid lines, and its normal);
//  - three arrows of the same colors move it along those axes, in depth too;
//  - the square in the middle moves it freely, parallel to the screen;
//  - the square on a crossing of the grid, just out of the rings, sizes the
//    cells (that crossing follows the pointer);
//  - the round button by the handles opens a menu: color, horizon, number of
//    cells, lens, and putting the rotation or position back.
//
// The handles keep their size on screen whatever the zoom. A drag shows the
// plane live and is one undo step; on a locked layer (or cel) the handles
// are shown faded and do nothing.
class PerspectiveGizmo
{
public:
    enum class Handle { None, Move, AxisX, AxisY, AxisZ, RingX, RingY, RingZ, Scale, Menu };

    explicit PerspectiveGizmo(document::DocumentController *document);

    // The current layer, if it is a visible perspective layer.
    std::optional<document::DocumentController::LayerRef> target() const;
    bool isDragging() const { return m_drag.has_value(); }
    Handle hovered() const { return m_hovered; }

    // The handle at a document point (None where there is none, or on a
    // locked layer).
    Handle handleAt(const QPointF &docPos, const ViewTransform &view) const;
    // A point that grabs `handle`, document coordinates (its tip, middle,
    // or a point of its ring); nothing if it is not shown.
    std::optional<QPointF> handlePoint(Handle handle, const ViewTransform &view) const;

    // A press on a handle starts its drag (true); elsewhere nothing (false).
    bool press(const QPointF &docPos, const ViewTransform &view);
    void move(const QPointF &docPos, const ViewTransform &view);
    // Ends the drag: true if it was a click on the menu button.
    bool release(const QPointF &docPos);
    // A drag cut short: the layer as it was before it.
    void cancel();
    // The handle under the pointer; true if that changed (repaint).
    bool hover(const QPointF &docPos, const ViewTransform &view);

    void paint(QPainter &painter, const ViewTransform &view) const;
    // The settings menu of the target layer (the caller shows it).
    QMenu *createMenu(QWidget *parent);

    // Sizes on screen, px.
    static constexpr double kRingRadius = 70.0;
    static constexpr double kAxisLength = 112.0;
    static constexpr double kPick = 7.0; // how near the pointer must be to grab

private:
    struct Axis
    {
        perspective::Vec3 local;  // in the plane's own space
        perspective::Vec3 u, v;   // its ring: u × v = local
    };
    static const std::array<Axis, 3> &axes();

    // Where everything is for a grid and a view, document coordinates.
    struct Geometry
    {
        perspective::Grid grid;
        perspective::Vec3 center3; // the plane's center, camera space
        QPointF center;
        double zoom = 1.0;
        double units = 1.0;  // plane units per document px, at the center's depth
        std::array<perspective::Vec3, 3> directions; // the axes, camera space
        std::array<std::optional<QPointF>, 3> tips;  // nothing when seen end on
        std::array<std::vector<QPointF>, 3> rings;   // closed, sampled
        std::optional<QPointF> corner;               // the scale handle
        QPointF menu;
    };
    std::optional<Geometry> geometry(const perspective::Grid &grid, const ViewTransform &view) const;
    const doc::Layer *targetLayer() const;
    bool locked() const;
    // The ring's point at `angle` (radians), drawn; nothing behind the eye.
    static std::optional<QPointF> ringPoint(const Geometry &g, int axis, double angle);
    void preview(const perspective::Grid &grid);

    document::DocumentController *m_document;
    Handle m_hovered = Handle::None;
    struct Drag
    {
        Handle handle = Handle::None;
        document::DocumentController::LayerRef ref;
        doc::LayerProperties before;
        perspective::Grid grid; // as shown now
        QPointF press, last;
        double zoom = 1.0;
        QPointF tangent; // a ring: its direction on screen at the press, document px per radian
        bool moved = false;
    };
    std::optional<Drag> m_drag;
};

} // namespace mothion::canvas
