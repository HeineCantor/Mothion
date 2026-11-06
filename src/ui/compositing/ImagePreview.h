#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>

namespace mothion::ui {

// Shows an image on the desk color, to look at closely: it starts fitted to the widget, the wheel zooms around the
// pointer, a drag with the left or the middle button (or the pen) moves it, a double click (or F, Home, Ctrl+0)
// fits it again and Ctrl+1 shows it at its true size. Pixels are drawn smooth up to twice their size, and as
// squares beyond, to see a grain.
class ImagePreview : public QWidget
{
    Q_OBJECT

public:
    explicit ImagePreview(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    const QImage &image() const { return m_image; }

    double zoom() const { return m_zoom; } // 1 is fitted
    QPointF offset() const { return m_offset; } // how far the image's center is from the widget's, in pixels
    // Pixels on the screen for one pixel of the image.
    double scale() const { return fitScale() * m_zoom; }
    // The point of the image (in its pixels) under a point of the widget.
    QPointF imagePointAt(const QPointF &widgetPos) const;

    void fit();
    void actualSize(); // one pixel of the image for one on the screen

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void enterEvent(QEnterEvent *event) override;

private:
    double fitScale() const; // the scale that makes the image fit, with a margin
    void zoomAround(const QPointF &widgetPos, double zoom);

    QImage m_image;
    double m_zoom = 1.0;
    QPointF m_offset;
    bool m_dragging = false;
    QPoint m_dragFrom;
};

} // namespace mothion::ui
