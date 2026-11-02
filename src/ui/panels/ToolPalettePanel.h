#pragma once

#include "tools/ToolState.h"

#include <QElapsedTimer>
#include <QWidget>

class QButtonGroup;

namespace mothion::ui {

// Vertical column of tool buttons, each with its key.
// Selecting a button sets ToolState::activeTool; external changes are mirrored.
//
// A tool key tapped selects the tool; held, the tool lasts only while it is
// held (the previous tool comes back on release). Also here: X swaps the
// colors, [ and ] step the paint tool's size through the size presets.
class ToolPalettePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPalettePanel(tools::ToolState *state, QWidget *parent = nullptr);

    // A key held at least this long is a hold, not a tap.
    static constexpr int kHoldMs = 300;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void stepSize(int direction);

    tools::ToolState *m_state;
    QButtonGroup *m_group;
    // The tool key down now, and the tool to go back to if it is held.
    int m_heldKey = 0;
    tools::ToolId m_heldTool = tools::ToolId::Brush;
    tools::ToolId m_toolBeforeHold = tools::ToolId::Brush;
    QElapsedTimer m_held;
};

} // namespace mothion::ui
