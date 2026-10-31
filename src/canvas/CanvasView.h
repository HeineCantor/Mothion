#pragma once

#include "canvas/DabSpacer.h"
#include "canvas/FrameRenderer.h"
#include "canvas/NavigationGesture.h"
#include "canvas/StrokeSession.h"
#include "canvas/ViewTransform.h"
#include "core/PenSample.h"
#include "core/StrokeSmoother.h"

#include <QWidget>

#include <optional>

class QPointingDevice;

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
// NavigationGesture (hand / zoom / rotate) or a stroke:
//   sample -> StrokeSmoother -> pressure curve -> DabSpacer -> StrokeSession,
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

private:
    void handleSample(const PenSample &sample);
    void noteDevice(const QPointingDevice *device);

    // Which navigation a press should start, given the active tool, the held
    // keys (Space + Ctrl/Shift) and the buttons. None means "paint".
    NavigationGesture::Mode navigationModeFor(const PenSample &sample) const;

    void beginStroke(const PenSample &sample);
    void continueStroke(const PenSample &sample);
    void endStroke();
    void cancelStroke(); // the controller interrupted the stroke (undo, frame change...)
    DabSpacer::Point strokePoint(const PenSample &smoothed) const;

    void renderWholeFrame();                     // frame, layers or timing changed
    void renderDocumentRect(const QRect &docRect); // pixels changed
    void updateDocumentRect(const QRect &docRect);
    void updateBrushOutline(const QPointF &newPos);
    QRectF brushOutlineRect() const; // view coordinates
    void updateCursor();
    void viewWasChanged();

    tools::ToolState *m_toolState;
    document::DocumentController *m_document;
    ViewTransform m_view;
    NavigationGesture m_gesture;
    FrameRenderer m_frame;
    std::optional<StrokeSession> m_stroke; // set while a stroke is in progress
    DabSpacer m_spacer;
    StrokeSmoother m_smoother;
    bool m_eraseStroke = false; // current stroke erases (eraser tool or eraser end)
    DebugOverlay *m_overlay;

    const QPointingDevice *m_lastDevice = nullptr;
    QPointF m_pointerPos;      // last pointer position (view coordinates)
    bool m_pointerVisible = false;
    InputTool m_pointerTool = InputTool::Mouse;
    bool m_spaceHeld = false;
    bool m_fittedOnce = false;
};

} // namespace mothion::canvas
