#pragma once

#include <QImage>
#include <QTimer>
#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace mothion::canvas {
class CanvasView;
}

namespace mothion::ui {

// Thumbnail of the whole document with the visible area outlined.
// Click or drag to move the view there.
class NavigatorPreview : public QWidget
{
    Q_OBJECT

public:
    explicit NavigatorPreview(canvas::CanvasView *canvas, QWidget *parent = nullptr);

    QSize sizeHint() const override { return {240, 150}; }

public slots:
    void scheduleThumbnail(); // coalesces many document changes into one refresh

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QRectF documentRect() const; // where the document is drawn inside the widget
    void rebuildThumbnail();

    canvas::CanvasView *m_canvas;
    QImage m_thumbnail;
    QTimer m_refresh;
};

// The navigator panel: preview plus zoom, rotation and mirroring controls.
//
// Unlike the other panels it talks to the canvas directly: it is a view of
// the view, so it needs the view state and the document image.
class NavigatorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NavigatorPanel(canvas::CanvasView *canvas, QWidget *parent = nullptr);

private:
    void syncFromView();

    canvas::CanvasView *m_canvas;
    NavigatorPreview *m_preview;
    QSlider *m_zoom;
    QLabel *m_zoomLabel;
    QSlider *m_rotation;
    QLabel *m_rotationLabel;
    QToolButton *m_flipH;
    QToolButton *m_flipV;
};

} // namespace mothion::ui
