#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

namespace mothion::ui {

// HSV picker: a hue ring around a saturation/value square.
//
// Keeps its own hue/saturation/value so the hue is not lost when the color
// is grey or black (QColor reports hue -1 there).
class ColorWheel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorWheel(QWidget *parent = nullptr);

    QColor color() const;
    void setColor(const QColor &color); // does not emit colorEdited

    QSize sizeHint() const override { return {200, 200}; }
    QSize minimumSizeHint() const override { return {120, 120}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return width; }

signals:
    void colorEdited(const QColor &color);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    enum class Drag { None, Hue, SatVal };

    QPointF center() const;
    double outerRadius() const;
    double innerRadius() const;
    QRectF squareRect() const;
    void rebuildSquare(); // the S/V image depends on the hue
    void editAt(const QPointF &pos);

    double m_hue = 0.0; // 0..1
    double m_sat = 0.0; // 0..1
    double m_val = 0.0; // 0..1
    Drag m_drag = Drag::None;
    QImage m_square;
};

} // namespace mothion::ui
