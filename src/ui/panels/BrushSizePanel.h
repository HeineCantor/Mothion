#pragma once

#include "tools/ToolState.h"

#include <QWidget>

namespace mothion::ui {

// A grid of preset brush sizes, each shown as its dot and number: a click
// sets the active tool's size, the cell of the current size is highlighted.
// The grid wraps to its width.
class BrushSizeGrid : public QWidget
{
    Q_OBJECT

public:
    explicit BrushSizeGrid(tools::ToolState *state, QWidget *parent = nullptr);

    static const QList<double> &presets();

    // The preset at a point of the grid, -1 for none.
    int presetAt(QPoint pos) const;
    QRect cellRect(int index) const;

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int columns() const { return columnsFor(width()); }
    static int columnsFor(int width);

    tools::ToolState *m_state;
    int m_hovered = -1;
};

// The brush size palette: the grid, scrolling when the panel is shorter.
class BrushSizePanel : public QWidget
{
    Q_OBJECT

public:
    explicit BrushSizePanel(tools::ToolState *state, QWidget *parent = nullptr);

    BrushSizeGrid *grid() const { return m_grid; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    BrushSizeGrid *m_grid;
};

} // namespace mothion::ui
