#pragma once

#include "canvas/NavigationGesture.h"
#include "canvas/ViewTransform.h"

#include <QWidget>

namespace mothion::tools {
class ToolState;
}

namespace mothion::canvas {

// The central drawing widget.
//
// It shows the document through a ViewTransform and routes pointer input
// either to a NavigationGesture (hand / zoom / rotate) or to painting.
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

signals:
    void viewChanged(double zoom, double rotationDegrees);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // Which navigation a press should start, given the active tool, the
    // held keys (Space + Ctrl/Shift) and the button. None = paint.
    NavigationGesture::Mode navigationModeFor(Qt::MouseButtons buttons) const;
    void updateCursor();
    void viewWasChanged();

    tools::ToolState *m_toolState;
    ViewTransform m_view;
    NavigationGesture m_gesture;
    bool m_spaceHeld = false;
    bool m_fittedOnce = false;
};

} // namespace mothion::canvas
