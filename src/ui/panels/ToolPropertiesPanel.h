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
// plus the global smoothing, pen tilt switch and pressure curve; for the puppet warp, its
// mesh options instead; for the fill, its area options.
class ToolPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPropertiesPanel(tools::ToolState *state, QWidget *parent = nullptr);

    PressureCurveEditor *curveEditor() const { return m_curveEditor; }

private:
    void syncFromState();
    void syncPuppetFromState();
    void syncFillFromState();

    tools::ToolState *m_state;
    QLabel *m_toolName;
    QWidget *m_brushControls;
    SliderField *m_size;
    SliderField *m_opacity;
    SliderField *m_smoothing;
    QCheckBox *m_useTilt;
    PressureCurveEditor *m_curveEditor;

    QWidget *m_puppetControls;
    QComboBox *m_meshDensity;
    QSpinBox *m_meshExpand;
    QCheckBox *m_showMesh;
    QComboBox *m_puppetMode;
    QSpinBox *m_puppetRadius;
    QLabel *m_puppetHelp;

    QWidget *m_fillControls;
    SliderField *m_fillOpacity;
    SliderField *m_tolerance;
    QSpinBox *m_fillExpand;
    QComboBox *m_fillSource;

    QLabel *m_transformHelp;
};

} // namespace mothion::ui
