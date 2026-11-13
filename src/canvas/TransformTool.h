#pragma once

#include "canvas/CanvasTool.h"
#include "core/document/Ids.h"
#include "core/document/TiledImage.h"
#include "core/document/VectorStroke.h"

#include <QCursor>
#include <QImage>
#include <QPainterPath>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QTransform>

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

// The cursors of the free transform, shared by the tool that moves a cel the same way (AnimateTool): a turning arrow, and
// the arrows of a handle (from the middle of the box to the handle, in the page).
QCursor turnCursor();
QCursor resizeCursor(const QPointF &from, const QPointF &to);

// Free transform of the selected pixels (all of them when nothing is
// selected) of the current layer, or of every layer chosen in the Layers
// panel (all those of a chosen frame or cel) together, in one box with eight
// handles around the selection's bounding box (the bounds of the pixels when
// nothing is selected): a corner scales (Shift keeps the proportions), a side scales one way,
// inside the box moves, outside rotates (Shift: 15° steps). The selection
// follows.
//
// The box comes up as soon as the tool is chosen and stays open across
// drags; Enter applies it as one undo step, Esc drops it, and either goes
// back to the tool used before. Another tool, a frame or layer change, or
// undo once there is nothing left to undo in it applies it too: while it is
// open undo and redo go through its drags.
class TransformTool : public CanvasTool
{
public:
    TransformTool(document::DocumentController *document, tools::ToolState *toolState,
                  std::function<void(const QString &)> status, std::function<void()> repaint);
    ~TransformTool() override;

    void activated() override;
    void press(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void move(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void release(const QPointF &docPos, Qt::KeyboardModifiers modifiers) override;
    void cancel() override;
    bool isActive() const override { return m_drag.has_value(); }
    bool hasPendingEdit() const override { return m_session != nullptr; }
    void commit() override;
    bool keyPress(int key) override;
    bool undoStep() override;
    bool redoStep() override;
    bool canUndoStep() const override { return m_session && !m_session->undo.empty() && !m_drag; }
    bool canRedoStep() const override { return m_session && !m_session->redo.empty() && !m_drag; }
    void setViewZoom(double zoom) override { m_zoom = zoom; }
    // While dragging, draw the pixels once per turn of the event loop, at the latest transform, instead of once per
    // pen sample: a pen sends them faster than a big layer can be redrawn, and each one would wait its turn. The
    // box follows at once; the release (and the end of the transform) draws what is pending. Off: every sample.
    void setDeferredRendering(bool on) { m_deferred = on; }
    // Draws the pixels again with the filter now chosen (Preferences::transformFilter()): the transform is as it was.
    void refreshRendering();
    void paintOverlay(QPainter &painter, const ViewTransform &view) const override;
    // Under the pointer: the arrows of a handle, all four inside the box, a turning arrow out of it.
    bool hover(const QPointF &docPos) override;
    QCursor cursor() const override;

    // The transform so far, from the pixels' place to where they are shown.
    QTransform transform() const { return m_session ? m_session->transform : QTransform(); }
    // The box being transformed, before the transform (document coordinates).
    QRect box() const { return m_session ? m_session->box : QRect(); }

private:
    enum class Grip { Move, Rotate, Corner, Side };
    struct Drag
    {
        Grip grip = Grip::Move;
        QTransform start;   // the transform at the press
        QPointF pressDoc;   // where the pen went down
        QPointF handle;     // the grabbed handle, in box coordinates
        QPointF anchor;     // the point that stays put, in box coordinates
        bool horizontal = false; // a side handle: which way it scales
    };

    struct Pick
    {
        Grip grip = Grip::Move;
        int handle = -1; // 0..7 for a corner or side
    };
    // What a press at `docPos` would grab.
    Pick pick(const QPointF &docPos) const;

    bool start();
    // Draws the transform: the layers of the drawing shown now (the others, of other frames, only once it is
    // applied: they are not in sight, and a cel of many frames would make every drag wait for all of them).
    void render();
    // All the layers, at the transform as it is; `onlyLive` the ones shown now.
    void renderParts(bool onlyLive);
    void requestRender(); // render(), or once the pending samples are in (see setDeferredRendering())
    void flushRender();
    // Draws the transform, or the drawing as it was when back at the start.
    void show();
    void finish(bool apply);
    // The eight handles in box coordinates: corners 0..3, then side middles.
    QPointF handlePoint(int index) const;

    document::DocumentController *m_document;
    tools::ToolState *m_toolState;
    std::function<void(const QString &)> m_status;
    std::function<void()> m_repaint;
    double m_zoom = 1.0;
    bool m_deferred = false;
    bool m_renderPending = false;
    bool m_rough = false; // what is drawn was made the fast way, while dragging: the chosen filter is yet to come
    QTimer m_renderTimer;

    struct Part // one layer transformed
    {
        doc::TiledImage *image = nullptr; // the layer
        doc::TiledImage before;           // as it was when the transform started
        doc::TiledImage rest;             // what stays put (outside the selection)
        doc::TiledImage moving;           // the pixels that move
        QImage source;                    // those, over the box (made when first drawn, for a layer out of sight)
        bool live = true;                 // in the drawing shown at the current frame
        // A vector layer: while the pen drags, its pixels are stretched like any layer's; once it is up (and when the
        // transform is applied) its strokes are drawn again at the transform, which is what keeps them sharp.
        bool vector = false;
        doc::CelId cel = doc::kNoId;
        doc::DrawingId drawing = doc::kNoId;
        doc::LayerId layer = doc::kNoId;
        // With a selection the strokes are cut along its outline: `strokes` are those inside (they move), `restStrokes`
        // those outside (they stay); `allStrokes` is the layer's list as it was when the transform started.
        std::vector<doc::VectorStroke> strokes, restStrokes, allStrokes;
    };
    struct Session
    {
        std::vector<Part> parts;
        QRect box;                        // the selection's bounding box (the pixels' bounds when nothing is selected)
        QPainterPath selection;           // as it was, empty if none
        QTransform transform;
        QRect shown; // area last drawn by the transform
        // A paste that floats: the pasted pixels (or strokes) are what moves, over the layer as it was; Esc drops it and
        // the selection goes back to what it was before.
        bool floating = false;
        QPainterPath priorSelection;
        bool changed = false;
        bool everythingDrawn = true; // the layers out of sight are at the transform too
        std::vector<QTransform> undo; // the transform before each drag
        std::vector<QTransform> redo; // those undone
    };
    std::unique_ptr<Session> m_session;
    std::optional<Drag> m_drag;
    std::optional<Pick> m_hover; // under the pointer, between drags
};

} // namespace mothion::canvas
