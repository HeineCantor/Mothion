#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/TiledImage.h"
#include "core/puppet/PuppetRig.h"
#include "core/puppet/WarpRaster.h"

#include <QMetaObject>
#include <QRect>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// Puppet warp of the current layer, or of every layer chosen in the Layers
// panel (all those of a chosen frame or cel) together, or of their selected
// part: the first click meshes the shape they draw together; clicking on it
// places pins, dragging a pin deforms the mesh around the others
// (puppet::PuppetRig), and the pixels follow (puppet::warp).
//
// The warp stays open across clicks; Enter, another tool, a frame or layer
// change, or undo once there is nothing left to undo in it applies it as one
// undo step, Esc drops it. While it is open undo and redo go through its own
// steps (each drag, pin added or removed). Alt+click removes a pin.
// Shift (or Ctrl) + click adds a pin to the selected ones or takes it out,
// on the drawing places one more; dragging a selected pin moves all of
// them together. Delete removes the selected pins.
//
// In local mode (PuppetSettings::local) there are no pins to place: all of
// the shape is held, a drag moves the point under the pen and bends what
// lies within the radius around it. Shift (or Ctrl) + click picks several
// points, a drag from one of them moves them all, each bending its disc.
// The wheel, or + and −, sizes the radius, shown as a circle around the
// points a drag would take; turned during a drag, it changes what bends.
class PuppetTool : public CanvasTool
{
public:
    PuppetTool(document::DocumentController *document, tools::ToolState *toolState,
               std::function<void(const QString &)> status, std::function<void()> repaint);
    ~PuppetTool() override;

    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_draggingPins || m_localDrag; }
    bool hasPendingEdit() const override { return m_session != nullptr; }
    void commit() override;
    bool keyPress(int key) override;
    bool wantsKey(int key) const override;
    void setViewZoom(double zoom) override { m_zoom = zoom; }
    void activated() override;
    bool hover(const QPointF &docPos) override;
    // The area the open warp shows now (document coordinates), once it has something to apply: a pin placed or a
    // drag done. A null rect otherwise (no warp, or only the mesh made for the radius preview).
    QRectF pendingBounds() const;
    // Pins placed in the open warp.
    std::size_t pinCount() const { return m_session ? m_session->rig->pins().size() : 0; }
    // Pins selected, and in radius mode the points picked, to move together.
    const std::vector<int> &selectedPins() const { return m_selectedPins; }
    std::size_t pickedPointCount() const { return m_localPoints.size(); }
    bool undoStep() override;
    bool redoStep() override;
    bool canUndoStep() const override { return m_session && !m_session->undo.empty() && !isActive(); }
    bool canRedoStep() const override { return m_session && !m_session->redo.empty() && !isActive(); }
    bool wheel(double steps, Qt::KeyboardModifiers modifiers) override;
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;

private:
    // Meshes the drawing; `quiet`: no status message if there is nothing to.
    bool start(bool quiet = false);
    // Radius mode shows what a drag would take before the first one: the
    // mesh is made as soon as it is needed, once per failed try.
    void startForPreview();
    // The point a drag would take, document coordinates.
    std::optional<QPointF> grabPoint() const;
    // A picked point (radius mode) within grabbing distance of `at`.
    std::optional<std::size_t> pickedPointAt(puppet::Vec2 at) const;
    void startPinDrag(puppet::Vec2 at);
    // The dragged pins, each moved by the pen's travel since the press.
    void moveDraggedPins(const QPointF &pen, int iterations);
    void clearSelection(); // pins and picked points: their indices may be gone
    // Radius mode: the radius times 1.15 per step (down for negative ones).
    void stepRadius(double steps);
    void render();
    void settingsChanged();

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    std::function<void()> m_repaint;
    double m_zoom = 1.0;

    struct Part // one layer warped
    {
        doc::TiledImage *image = nullptr; // the layer
        doc::TiledImage before;           // as it was when the warp started
        doc::TiledImage rest;             // what stays put (outside the selection)
        puppet::RasterImage source;       // the pixels being warped, undeformed
    };
    struct Session
    {
        std::vector<Part> parts; // all bent by the one rig
        std::unique_ptr<puppet::PuppetRig> rig;
        QRect shown;                      // area last drawn by the warp
        bool changed = false;
        // The warp's own steps: the rig as it was before each, and those undone.
        std::vector<puppet::PuppetRig::State> undo;
        std::vector<puppet::PuppetRig::State> redo;
        std::optional<puppet::PuppetRig::State> stepStart; // at the press, until the step is known
        bool stepChanged = false;
    };
    // A step starts at the press; it is kept if something changed.
    void beginStep();
    void endStep();
    void pushStep(puppet::PuppetRig::State before);
    // Draws the rig, or the drawing as it was when the rig is undeformed.
    void show();
    std::unique_ptr<Session> m_session;
    // Pins mode: the selected pins, and the drag of them in progress.
    std::vector<int> m_selectedPins;
    bool m_draggingPins = false;
    bool m_dragMoved = false;
    std::vector<int> m_dragPins;
    std::vector<puppet::Vec2> m_dragStarts; // where each was at the press
    puppet::Vec2 m_pressAt;
    // Radius mode: the points picked to move together, and the drag.
    std::vector<puppet::PuppetRig::VertexRef> m_localPoints;
    bool m_localDrag = false;
    QPointF m_pointer; // last pen position, for the radius circle
    bool m_pointerKnown = false;
    bool m_previewFailed = false; // nothing to mesh, as of the last try
    QMetaObject::Connection m_settingsConnection;
    std::vector<QMetaObject::Connection> m_documentConnections; // a failed preview may work after them
};

} // namespace mothion::canvas
