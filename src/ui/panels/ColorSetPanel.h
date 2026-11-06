#pragma once

#include "tools/ColorSetLibrary.h"
#include "tools/ToolState.h"

#include <QTimer>
#include <QWidget>

class QComboBox;
class QToolButton;

namespace mothion::ui {

// The swatches of one color set, wrapped to the width. A click sets the main color. A swatch held down (or
// right-clicked) offers rename, a color picker for it and delete; dragged, it moves to another place.
// The built-in set can only be clicked.
class ColorSwatchGrid : public QWidget
{
    Q_OBJECT

public:
    ColorSwatchGrid(tools::ToolState *state, tools::ColorSetLibrary *library, QWidget *parent = nullptr);

    // The swatch at a point of the grid, -1 for none.
    int swatchAt(QPoint pos) const;
    QRect cellRect(int index) const;
    // Opens a color picker (not blocking) for swatch `index` of the shown set; the color is set when it is accepted.
    void editColor(int index);
    // The set shown: the library's active one.
    const tools::ColorSet *current() const { return m_library->set(m_library->activeId()); }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    bool event(QEvent *event) override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int columns() const;
    int dropIndexAt(QPoint pos) const;
    void showMenu(int index, const QPoint &globalPos);

    tools::ToolState *m_state;
    tools::ColorSetLibrary *m_library;
    QTimer m_hold; // a swatch held down: its menu
    int m_pressed = -1;
    QPoint m_pressPos;
    bool m_dragging = false;
    bool m_menuShown = false;
    int m_drop = -1;
    int m_hovered = -1;
};

// The Color set dock: the set chosen from a list, its swatches, a button to add the current color and a menu for
// the sets (new, duplicate, rename, delete). The sets are the library's, kept for every project.
class ColorSetPanel : public QWidget
{
    Q_OBJECT

public:
    ColorSetPanel(tools::ToolState *state, tools::ColorSetLibrary *library, QWidget *parent = nullptr);

    ColorSwatchGrid *grid() const { return m_grid; }
    QComboBox *setList() const { return m_sets; }
    QToolButton *addButton() const { return m_add; }

private:
    void fillList();
    void syncButtons();
    void newSet();
    void renameSet();
    void deleteSet();

    tools::ToolState *m_state;
    tools::ColorSetLibrary *m_library;
    QComboBox *m_sets;
    QToolButton *m_add;
    QToolButton *m_menu;
    QAction *m_rename = nullptr;
    QAction *m_delete = nullptr;
    ColorSwatchGrid *m_grid;
};

} // namespace mothion::ui
