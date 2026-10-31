#pragma once

#include "tools/ToolState.h"

#include <QWidget>

class QButtonGroup;

namespace mothion::ui {

// Vertical column of tool buttons (brush, eraser, hand, zoom).
// Selecting a button sets ToolState::activeTool; external changes are mirrored.
class ToolPalettePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPalettePanel(tools::ToolState *state, QWidget *parent = nullptr);

private:
    QButtonGroup *m_group;
};

} // namespace mothion::ui
