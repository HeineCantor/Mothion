#pragma once

#include "tools/BrushPreset.h"

#include <QWidget>

#include <map>

class QLabel;
class QListWidget;

namespace mothion::ui {

// Lists the presets ("sub tools") of the active tool, each with a stroke
// preview rendered by the real stroke engine. Clicking one applies it.
class SubToolPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SubToolPanel(tools::ToolState *state, QWidget *parent = nullptr);

private:
    void showPresetsFor(tools::ToolId tool);

    tools::ToolState *m_state;
    QListWidget *m_list;
    QLabel *m_empty;
    QList<tools::BrushPreset> m_presets;     // of the tool currently shown
    std::map<tools::ToolId, int> m_selected; // remembered selection per tool
};

} // namespace mothion::ui
