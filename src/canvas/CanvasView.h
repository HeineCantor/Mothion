#pragma once

#include "canvas/CanvasTool.h"
#include "canvas/FrameRenderer.h"
#include "canvas/NavigationGesture.h"
#include "canvas/PerspectiveGizmo.h"
#include "canvas/PerspectiveSnap.h"
#include "canvas/StrokeSession.h"
#include "canvas/ViewTransform.h"
#include "core/PenSample.h"
#include "core/document/TiledImage.h"
#include "core/StrokeSmoother.h"
#include "core/brush/DabGenerator.h"
#include "core/brush/StrokeShaper.h"
#include "core/brush/VelocityMeter.h"

#include <QWidget>

#include <map>
#include <memory>
#include <deque>
#include <optional>
#include <vector>

class QPointingDevice;
class QTimer;

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

class DebugOverlay;

// The central drawing widget.
//
// Input path: Qt tablet/mouse event -> input::toPenSample() -> handleSample().
// From handleSample() on, only PenSamples are used. A sample either drives a
// NavigationGesture (hand / zoom / rotate), the handles of the current
// perspective layer (PerspectiveGizmo, when pressed on one), a CanvasTool
// (move...) or a stroke:
//   sample -> StrokeSmoother -> brush::BrushInput (pressure curve, velocity, tilt)
//          -> brush::DabGenerator -> StrokeSession (DabStamper),
// painting into the drawing the DocumentController picks for the current
// layer and frame. What is shown is the FrameRenderer's composition of the
// current frame, refreshed from the controller's change signals.
class CanvasView : public QWidget
{
    Q_OBJECT

public:
    CanvasView(tools::ToolState *toolState, document::DocumentController *document, QWidget *parent = nullptr);

    const ViewTransform &view() const { return m_view; }
    // The document pixels as displayed (for thumbnails such as the navigator).
    const QImage &documentImage() const { return m_frame.image(); }
    // The selection outline's ants march: black and white dashes swap at
    // regular intervals while there is a selection. 0 or 1.
    int selectionAntsPhase() const { return m_antsPhase; }

    // The frames whose drawings the onion skin shows now, nearest first,
    // before (negative distance) and after (positive): one per drawing.
    QList<int> onionSkinFrames() const;

    // The handles of the current perspective layer.
    const PerspectiveGizmo &perspectiveGizmo() const { return m_gizmo; }
    // Where the open transform's box is now (document coordinates, the box
    // after the transform); a null rect when no transform is open.
    QRectF transformBounds() const;
    // Applies (true) or drops the open transform, as Enter and Esc do.
    void finishTransform(bool apply);
    // The same for a polygon or curve in progress: the box around its points
    // (null when none), whether it has enough points to finish, and OK (true) or Cancel.
    // And for the open puppet warp: the area it shows (null while it has nothing to apply), and OK / Cancel.
    QRectF puppetBounds() const;
    void finishPuppet(bool apply);
    QRectF shapeBounds() const;
    bool shapeCanFinish() const;
    void finishShape(bool apply);

    // Undo and redo inside an edit left open by the active tool (the drags
    // of a puppet warp or a transform); false if there is no step to take,
    // and the document's history should go on.
    bool undoInEdit();
    bool redoInEdit();
    bool canUndoInEdit() const;
    bool canRedoInEdit() const;

public slots:
    void zoomIn();
    void zoomOut();
    void fitToView();
    void rotateLeft();
    void rotateRight();
    void resetRotation();
    void actualSize();                // zoom 100 %
    void setZoom(double zoom);        // around the viewport center
    void setRotation(double degrees); // around the viewport center
    void setFlipHorizontal(bool flipped);
    void setFlipVertical(bool flipped);
    void centerOn(const QPointF &docPoint);
    void setPenProximity(const mothion::ProximityEvent &event);
    void setDebugOverlayVisible(bool visible);

signals:
    // Zoom, rotation, mirroring or pan changed (read the rest from view()).
    void viewChanged(double zoom, double rotationDegrees);
    // Document pixels changed in this rect (document coordinates).
    void documentChanged(const QRect &docRect);
    // Every input sample, before smoothing (for the debug overlay and the curve editor).
    void sampleReceived(const mothion::PenSample &sample);
    // Emitted when the device behind the samples changes.
    void deviceChanged(const mothion::InputDeviceInfo &device);
    // Something worth telling the user (e.g. why a stroke was refused).
    void statusMessage(const QString &text);
    // The steps of an open edit may have changed (see undoInEdit()).
    void editStepsChanged();
    // The transform box came up, moved or went away (see transformBounds()).
    void transformChanged();
    // The points of a polygon or curve in progress changed (see shapeBounds()).
    void shapeChanged();
    // The puppet warp opened, changed or closed (see puppetBounds()).
    void puppetChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override; // ShortcutOverride: keys the active tool takes first
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    // Tab is a canvas key (held = pan), not focus navigation.
    bool focusNextPrevChild(bool) override { return false; }

private:
    void handleSample(const PenSample &sample);
    void noteDevice(const QPointingDevice *device);

    // Which navigation a press should start, given the active tool, the held
    // keys (Space or Tab + Ctrl/Shift) and the buttons. None means "paint".
    NavigationGesture::Mode navigationModeFor(const PenSample &sample) const;

    void beginStroke(const PenSample &sample);
    void continueStroke(const PenSample &sample);
    void endStroke();
    void cancelStroke(); // the controller interrupted the stroke or edit (undo, frame change...)
    // The canvas tool behind the active palette tool, if it is one (move...).
    CanvasTool *canvasTool() const;
    void commitPendingEdits(); // of the canvas tools that are no longer active
    brush::BrushInput brushInput(const PenSample &smoothed);
    // Records a stroke point and draws it.
    void addStrokePoint(const brush::BrushInput &in);
    // A pen sample of the stroke: straight to the stabilizer, or with sharp
    // angles held until the corner detector has judged it.
    void takeSample(const PenSample &sample);
    // The same once perspective snapping has had its say.
    void takeRawSample(const PenSample &sample);
    PenSample snappedSample(const PenSample &sample) const;
    // Takes the next axis for the snapped stroke in progress (the A key) and draws the whole stroke again on it.
    void cycleSnapAxis();
    // The outline and the letter of the axis the stroke in progress is snapped to.
    void paintSnapGuide(QPainter &p) const;
    // Shift held, with a paint tool: a straight line from the last point drawn, ending at the pen (shown as
    // it moves); touching places it, letting go of Shift drops it.
    QPointF lineStartOf(const std::vector<brush::BrushInput> &points) const;
    void updateStraightLine(); // armed while Shift is held, with a paint tool and a point to start from
    void placeStraightLine(const PenSample &touch);
    void paintStraightLinePreview(QPainter &p) const;
    // The held samples now judged (all of them at the end of the stroke): a
    // corner restarts the stabilizer from itself and gets its point painted.
    void releaseHeldSamples(bool end);
    // Draws the recorded points not drawn yet.
    void drawPendingPoints();

    void renderWholeFrame();                     // frame, layers or timing changed
    void renderDocumentRect(const QRect &docRect); // pixels changed
    void updateDocumentRect(const QRect &docRect);
    void updateBrushOutline(const QPointF &newPos);
    QRectF brushOutlineRect() const; // view coordinates, at the current size and zoom
    void updateCursor();
    void viewWasChanged();

    tools::ToolState *m_toolState;
    document::DocumentController *m_document;
    ViewTransform m_view;
    NavigationGesture m_gesture;
    FrameRenderer m_frame;
    std::optional<StrokeSession> m_stroke; // set while a stroke is in progress
    std::unique_ptr<CanvasTool> m_moveTool;
    std::unique_ptr<CanvasTool> m_rectSelectTool;
    std::unique_ptr<CanvasTool> m_lassoSelectTool;
    std::unique_ptr<CanvasTool> m_puppetTool;
    std::unique_ptr<CanvasTool> m_eyedropperTool;
    std::unique_ptr<CanvasTool> m_fillTool;
    std::unique_ptr<CanvasTool> m_blendTool;
    std::unique_ptr<CanvasTool> m_liquifyTool;
    std::unique_ptr<CanvasTool> m_shapeTool;
    std::unique_ptr<CanvasTool> m_transformTool;
    CanvasTool *m_editTool = nullptr; // the canvas tool between its press and release
    PerspectiveGizmo m_gizmo;
    void updateGizmoCursor(); // over a handle: the hand
    std::optional<brush::DabGenerator> m_dabs;
    std::vector<brush::BrushInput> m_points; // the stroke so far, for the final pass
    std::size_t m_drawnPoints = 0;           // points already given to m_dabs
    std::uint32_t m_seed = 0;                // the stroke's random seed, reused by the final pass
    brush::VelocityMeter m_velocity;
    StrokeSmoother m_smoother;
    brush::CornerDetector m_corners; // on the raw pen points, screen pixels
    std::deque<PenSample> m_heldSamples; // waiting for the corner detector
    bool m_sharpCorners = false;     // for the stroke in progress
    PerspectiveSnap m_snap;
    bool m_snapping = false;             // the stroke in progress is snapped
    std::vector<PenSample> m_snapHeld;   // its first samples, until the axis is chosen
    std::vector<PenSample> m_snapRaw;    // all its samples, to draw it again on another axis
    QPointF m_snapPen;                   // where the pen is, document coordinates
    bool m_eraseStroke = false; // current stroke erases (eraser tool or eraser end)
    DebugOverlay *m_overlay;

    const QPointingDevice *m_lastDevice = nullptr;
    QPointF m_pointerPos;      // last pointer position (view coordinates)
    bool m_pointerVisible = false;
    // Area of the brush outline as last painted: what to repaint to erase it,
    // whatever changed since (size, zoom, eraser end).
    QRect m_paintedOutline;
    InputTool m_pointerTool = InputTool::Mouse;
    // Held keys that turn the pen into the hand (Ctrl: zoom, Shift: rotate).
    bool m_spaceHeld = false;
    bool m_tabHeld = false;
    bool panKeyHeld() const { return m_spaceHeld || m_tabHeld; }
    // Alt held with a paint tool: the pen picks colors instead.
    bool m_altHeld = false;
    std::optional<QPointF> m_lastPoint; // where the last stroke ended, document coordinates
    const doc::TiledImage *m_strokeImage = nullptr; // what the stroke in progress paints on
    bool m_lineArmed = false;           // the straight line waits for the pen to touch
    bool m_shiftHeld = false;
    bool m_placingLine = false;         // the stroke in progress is that line: full pressure, no stabilizer
    bool picksColor() const;
    // The frame's color at a document point, invalid off the sheet.
    QColor colorAt(const QPointF &docPos) const;
    bool *panKeyFlag(int key); // the flag of a pan key, nullptr for other keys
    bool m_fittedOnce = false;
    // Onion skin: the current layer's drawings around the current frame,
    // tinted (cached per drawing, made again when it or its color changes).
    struct OnionImage
    {
        QPoint origin; // document coordinates of its top-left
        QImage tinted;
        QRgb color = 0;
    };
    void paintOnionSkin(QPainter &painter);
    const OnionImage &onionImage(doc::DrawingId id, const QColor &color);
    std::map<doc::DrawingId, OnionImage> m_onionImages;
    // Runs the ants while there is a selection, stops them otherwise.
    void updateAnts();
    QTimer *m_antsTimer;
    int m_antsPhase = 0;
};

} // namespace mothion::canvas
