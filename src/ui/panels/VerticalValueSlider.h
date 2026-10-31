#pragma once

#include <QWidget>

#include <functional>

namespace mothion::ui {

// Tall slider for quick, pen-friendly edits of one value (brush size, opacity).
// The current value is shown in a round badge on top; dragging anywhere on the
// track sets the value directly (top = maximum). The mouse wheel nudges it.
class VerticalValueSlider : public QWidget
{
    Q_OBJECT

public:
    enum class Scale {
        Linear,
        Logarithmic, // equal travel per doubling: fine control on small values
    };
    using Formatter = std::function<QString(double)>;

    VerticalValueSlider(double minimum, double maximum, Scale scale, QWidget *parent = nullptr);

    // Text for the badge; may contain a '\n' (e.g. "12.0\npx").
    void setFormatter(Formatter formatter);

    double value() const { return m_value; }
    void setValue(double value); // does not emit valueEdited

    QSize sizeHint() const override { return {44, 320}; }
    QSize minimumSizeHint() const override { return {36, 140}; }

signals:
    void valueEdited(double value); // only on user edits

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QRectF badgeRect() const;
    QRectF trackRect() const;
    double positionOf(double value) const; // 0 (bottom) .. 1 (top)
    double valueAt(double position) const;
    void editTo(double value);

    double m_min;
    double m_max;
    Scale m_scale;
    double m_value;
    Formatter m_formatter;
};

} // namespace mothion::ui
