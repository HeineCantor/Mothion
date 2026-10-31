#pragma once

#include "core/PressureCurve.h"

#include <QWidget>

namespace mothion::ui {

// Square editor for a PressureCurve: x = raw pen pressure, y = effective pressure.
// The two Bézier control points can be dragged. A marker shows the last
// pressure coming from the pen, to see the mapping live while testing.
class PressureCurveEditor : public QWidget
{
    Q_OBJECT

public:
    explicit PressureCurveEditor(QWidget *parent = nullptr);

    void setCurve(const PressureCurve &curve);
    const PressureCurve &curve() const { return m_curve; }

    QSize sizeHint() const override { return {180, 180}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return width; }

public slots:
    // Raw pressure of the current pen sample, or a negative value to hide the marker.
    void showInputPressure(float pressure);

signals:
    void curveEdited(const mothion::PressureCurve &curve);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRectF plotRect() const;
    QPointF toWidget(PressureCurve::Point p) const;
    PressureCurve::Point toCurve(QPointF w) const;

    PressureCurve m_curve;
    int m_dragHandle = -1; // -1 none, 0 = control1, 1 = control2
    float m_inputPressure = -1.0f;
};

} // namespace mothion::ui
