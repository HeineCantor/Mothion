#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>

namespace mothion::ui {

// Shows an image on the desk color, to look at closely: it starts fitted to the widget, the wheel zooms around the
// pointer, a drag with the left or the middle button (or the pen) moves it, a double click (or F, Home, Ctrl+0)
// fits it again and Ctrl+1 shows it at its true size. Pixels are drawn smooth up to twice their size, and as
// squares beyond, to see a grain. With a drawing tool chosen, the left button draws a rectangle or a free shape
// instead (the middle button still moves the image), and the shapes are handed over in pixels of the image;
// an overlay (a tint over the chosen areas, say) is painted over the picture, at its place.
class ImagePreview : public QWidget
{
    Q_OBJECT

public:
    enum class Tool { Pan, Rectangle, Lasso };

    explicit ImagePreview(QWidget *parent = nullptr);

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    // A picture drawn over the image, as big as it (null: none).
    void setOverlay(const QImage &overlay);
    bool hasOverlay() const { return !m_overlay.isNull(); }

    void setImage(const QImage &image);
    const QImage &image() const { return m_image; }

    double zoom() const { return m_zoom; } // 1 is fitted
    QPointF offset() const { return m_offset; } // how far the image's center is from the widget's, in pixels
    // Pixels on the screen for one pixel of the image.
    double scale() const { return fitScale() * m_zoom; }
    // The point of the image (in its pixels) under a point of the widget.
    QPointF imagePointAt(const QPointF &widgetPos) const;

    void fit();
    void zoomBy(double factor); // around the center of the widget
    void actualSize(); // one pixel of the image for one on the screen

signals:
    // A shape was drawn with the Rectangle or the Lasso tool, kept inside the image.
    void rectangleDrawn(const QRectF &imageRect);
    void lassoDrawn(const QPolygonF &imagePoints);

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

    QPointF toWidget(const QPointF &imagePoint) const;
    void finishShape();

    QImage m_image;
    QImage m_overlay;
    Tool m_tool = Tool::Pan;
    bool m_drawing = false;       // a shape is being drawn
    QPointF m_shapeStart;         // in pixels of the image
    QPolygonF m_shape;            // the Lasso so far, or the two corners of the Rectangle
    double m_zoom = 1.0;
    QPointF m_offset;
    bool m_dragging = false;
    QPoint m_dragFrom;
};

} // namespace mothion::ui
