#pragma once

#include "canvas/CanvasTool.h"
#include "canvas/FrameRenderer.h"
#include "canvas/NavigationGesture.h"
#include "canvas/StrokeSession.h"
#include "canvas/ViewTransform.h"
#include "core/PenSample.h"
#include "core/StrokeSmoother.h"
#include "core/brush/DabGenerator.h"
#include "core/brush/StrokeShaper.h"
#include "core/brush/VelocityMeter.h"

#include <QWidget>

#include <memory>
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
// NavigationGesture (hand / zoom / rotate), a CanvasTool (move...) or a stroke:
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

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
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
    // Records a stroke point (the anti-jitter filter may replace the last
    // one instead) and draws the points that are settled.
    void addStrokePoint(const brush::BrushInput &in);
    // Draws the recorded points not drawn yet; `all` includes the last,
    // which the anti-jitter filter otherwise holds back.
    void drawPendingPoints(bool all);

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
    CanvasTool *m_editTool = nullptr; // the canvas tool between its press and release
    std::optional<brush::DabGenerator> m_dabs;
    std::vector<brush::BrushInput> m_points; // the stroke so far, for the final pass
    std::size_t m_drawnPoints = 0;           // points already given to m_dabs
    std::uint32_t m_seed = 0;                // the stroke's random seed, reused by the final pass
    brush::VelocityMeter m_velocity;
    StrokeSmoother m_smoother;
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
    bool picksColor() const;
    // The frame's color at a document point, invalid off the sheet.
    QColor colorAt(const QPointF &docPos) const;
    bool *panKeyFlag(int key); // the flag of a pan key, nullptr for other keys
    bool m_fittedOnce = false;
    // Runs the ants while there is a selection, stops them otherwise.
    void updateAnts();
    QTimer *m_antsTimer;
    int m_antsPhase = 0;
};

} // namespace mothion::canvas
