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

// Two overlapping squares that keep their places: main color in front, secondary behind, the selected one
// outlined (a click selects one), and under them a rounded bar with the checkerboard of the transparent color
// (clicking it selects it: the brush then erases).
class ColorSwatchPair : public QWidget
{
    Q_OBJECT

public:
    explicit ColorSwatchPair(QWidget *parent = nullptr);
    void setColors(const QColor &main, const QColor &secondary, bool secondarySelected);
    void setTransparentSelected(bool on);
    QSize sizeHint() const override { return {kSquares, kSquares + kBarGap + kBarHeight}; }

    static constexpr int kSquares = 44;  // the two squares' area
    static constexpr int kBarGap = 4;
    static constexpr int kBarHeight = 14;

signals:
    void secondaryClicked();
    void mainClicked();
    void transparentClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF mainRect() const;
    QRectF secondaryRect() const;
    QRectF transparentRect() const;
    bool m_transparentSelected = false;
    QColor m_main;
    QColor m_secondary;
    bool m_secondarySelected = false;
};

} // namespace mothion::ui
