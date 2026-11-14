#pragma once

#include "tools/BrushPreset.h"

#include <QWidget>

#include <map>
#include <optional>

class QLabel;
class QListWidget;

namespace mothion::tools {
class BrushLibrary;
class SubToolMemory;
}

namespace mothion::ui {

// Lists the presets ("sub tools") of the active tool, built-in and imported,
// each with a stroke preview rendered by the real stroke engine. Clicking
// one applies it.
class SubToolPanel : public QWidget
{
    Q_OBJECT

public:
    // `memory` keeps what each paint sub tool is set to; without one the panel makes its own, for the session only.
    SubToolPanel(tools::ToolState *state, tools::BrushLibrary *library, tools::SubToolMemory *memory = nullptr,
                 QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    void showPresetsFor(tools::ToolId tool);

    tools::ToolState *m_state;
    tools::BrushLibrary *m_library;
    tools::SubToolMemory *m_memory;
    QListWidget *m_list;
    QLabel *m_empty;
    QList<tools::BrushPreset> m_presets;     // of the tool currently shown
    std::optional<tools::ToolId> m_shownTool;
    std::map<tools::ToolId, int> m_selected; // remembered selection per tool
};

} // namespace mothion::ui
