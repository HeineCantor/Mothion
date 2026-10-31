#pragma once

#include "tools/ToolState.h"

#include <QWidget>

class QLabel;

namespace mothion::ui {

class PressureCurveEditor;
class SliderField;

// Properties of the active tool: size and opacity (paint tools only),
// plus the global smoothing and pressure curve.
class ToolPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPropertiesPanel(tools::ToolState *state, QWidget *parent = nullptr);

    PressureCurveEditor *curveEditor() const { return m_curveEditor; }

private:
    void syncFromState();

    tools::ToolState *m_state;
    QLabel *m_toolName;
    SliderField *m_size;
    SliderField *m_opacity;
    SliderField *m_smoothing;
    PressureCurveEditor *m_curveEditor;
};

} // namespace mothion::ui
