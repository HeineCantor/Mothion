#pragma once

#include "tools/ToolState.h"

#include <QWidget>

class QLabel;
class QLineEdit;

namespace mothion::ui {

class ColorWheel;
class ColorSwatchPair;

// Color wheel with the main/secondary color pair and an HSV / hex readout.
class ColorWheelPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorWheelPanel(tools::ToolState *state, QWidget *parent = nullptr);

private:
    void showColor(const QColor &color);

    tools::ToolState *m_state;
    ColorWheel *m_wheel;
    ColorSwatchPair *m_swatches;
    QLabel *m_hsv;
    QLineEdit *m_hex;
};

// Two overlapping squares: main color in front, secondary behind.
// Clicking the back one swaps them.
class ColorSwatchPair : public QWidget
{
    Q_OBJECT

public:
    explicit ColorSwatchPair(QWidget *parent = nullptr);
    void setColors(const QColor &main, const QColor &secondary);
    QSize sizeHint() const override { return {44, 44}; }

signals:
    void swapRequested();
    void mainClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF mainRect() const;
    QRectF secondaryRect() const;
    QColor m_main;
    QColor m_secondary;
};

} // namespace mothion::ui
