#pragma once

#include "tools/ToolState.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace mothion::ui {

class PressureCurveEditor;
class SliderField;

// Properties of the active tool: size and opacity (paint tools only),
// plus the global smoothing and pressure curve; for the puppet warp, its
// mesh options instead.
class ToolPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPropertiesPanel(tools::ToolState *state, QWidget *parent = nullptr);

    PressureCurveEditor *curveEditor() const { return m_curveEditor; }

private:
    void syncFromState();
    void syncPuppetFromState();

    tools::ToolState *m_state;
    QLabel *m_toolName;
    QWidget *m_brushControls;
    SliderField *m_size;
    SliderField *m_opacity;
    SliderField *m_smoothing;
    PressureCurveEditor *m_curveEditor;

    QWidget *m_puppetControls;
    QComboBox *m_meshDensity;
    QSpinBox *m_meshExpand;
    QCheckBox *m_showMesh;
};

} // namespace mothion::ui
