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

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// Puppet warp of the drawing at the current layer and frame (or of its
// selected part): the first click meshes the drawn shape; clicking on it
// places pins, dragging a pin deforms the mesh around the others
// (puppet::PuppetRig), and the pixels follow (puppet::warp).
//
// The warp stays open across clicks; Enter, another tool, undo or a frame
// or layer change applies it as one undo step, Esc drops it. Alt+click or
// Delete removes a pin.
//
// In local mode (PuppetSettings::local) there are no pins to place: all of
// the shape is held, a drag moves the point under the pen and bends what
// lies within the radius around it. The wheel sizes the radius, shown as a
// circle around the pen.
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
    bool isActive() const override { return m_dragPin.has_value() || m_localDrag; }
    bool hasPendingEdit() const override { return m_session != nullptr; }
    void commit() override;
    bool keyPress(int key) override;
    void setViewZoom(double zoom) override { m_zoom = zoom; }
    bool hover(const QPointF &docPos) override;
    bool wheel(double steps, Qt::KeyboardModifiers modifiers) override;
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;

private:
    bool start();
    void render();
    void settingsChanged();

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    std::function<void()> m_repaint;
    double m_zoom = 1.0;

    struct Session
    {
        doc::TiledImage *image = nullptr; // the drawing being warped
        doc::TiledImage before;           // as it was when the warp started
        doc::TiledImage rest;             // what stays put (outside the selection)
        puppet::RasterImage source;       // the pixels being warped, undeformed
        std::unique_ptr<puppet::PuppetRig> rig;
        QRect shown;                      // area last drawn by the warp
        bool changed = false;
    };
    std::unique_ptr<Session> m_session;
    std::optional<int> m_dragPin;
    bool m_dragMoved = false;
    puppet::Vec2 m_grabOffset;  // pin position − pen position at the press
    std::optional<int> m_selectedPin;
    bool m_localDrag = false;
    QPointF m_pointer; // last pen position, for the radius circle
    bool m_pointerKnown = false;
    QMetaObject::Connection m_settingsConnection;
};

} // namespace mothion::canvas
