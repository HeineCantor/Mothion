#pragma once

#include "canvas/NavigationGesture.h"
#include "canvas/RasterSurface.h"
#include "canvas/StrokeRenderer.h"
#include "canvas/ViewTransform.h"
#include "core/PenSample.h"
#include "core/StrokeSmoother.h"

#include <QWidget>

class QPointingDevice;

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// The central drawing widget.
//
// Input path: Qt tablet/mouse event -> input::toPenSample() -> handleSample().
// From handleSample() on, only PenSamples are used. A sample either drives a
// NavigationGesture (hand / zoom / rotate) or a stroke:
//   sample -> StrokeSmoother -> pressure curve -> StrokeRenderer -> RasterSurface.
class CanvasView : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasView(tools::ToolState *toolState, QWidget *parent = nullptr);

    const ViewTransform &view() const { return m_view; }

public slots:
    void zoomIn();
    void zoomOut();
    void fitToView();
    void rotateLeft();
    void rotateRight();
    void resetRotation();
    void setPenProximity(const mothion::ProximityEvent &event);

signals:
    void viewChanged(double zoom, double rotationDegrees);
    // Every input sample, before smoothing (for the debug overlay and the curve editor).
    void sampleReceived(const mothion::PenSample &sample);
    // Emitted when the device behind the samples changes.
    void deviceChanged(const mothion::InputDeviceInfo &device);

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
    StrokeRenderer::Point strokePoint(const PenSample &smoothed) const;

    void updateDocumentRect(const QRect &docRect);
    void updateBrushOutline(const QPointF &newPos);
    QRectF brushOutlineRect() const; // view coordinates
    void updateCursor();
    void viewWasChanged();

    tools::ToolState *m_toolState;
    ViewTransform m_view;
    NavigationGesture m_gesture;
    RasterSurface m_surface;
    StrokeRenderer m_renderer;
    StrokeSmoother m_smoother;
    bool m_eraseStroke = false; // current stroke erases (eraser tool or eraser end)

    const QPointingDevice *m_lastDevice = nullptr;
    QPointF m_pointerPos;      // last pointer position (view coordinates)
    bool m_pointerVisible = false;
    InputTool m_pointerTool = InputTool::Mouse;
    bool m_spaceHeld = false;
    bool m_fittedOnce = false;
};

} // namespace mothion::canvas
