#pragma once

#include "tools/ToolState.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace mothion::ui {

class AdvancedToolSettings;
class PressureCurveEditor;
class SliderField;

// Properties of the active tool: size and opacity (paint tools only),
// plus the global smoothing, pen tilt switch and pressure curve (the rarer brush
// options are in AdvancedToolSettings, behind the wrench button); for the puppet warp, its
// mesh options instead; for the fill, its area options.
class ToolPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPropertiesPanel(tools::ToolState *state, QWidget *parent = nullptr);

    PressureCurveEditor *curveEditor() const { return m_curveEditor; }
    // A bar with the wrench (Advanced Tool Settings), for the dock to keep
    // under the panel, outside its scrolling (see PanelDock).
    QWidget *footer() const { return m_footer; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncFromState();
    void syncPuppetFromState();
    void syncFillFromState();
    void showAdvancedSettings();

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
    SliderField *m_puppetRadius;
    QLabel *m_puppetHelp;

    QWidget *m_fillControls;
    SliderField *m_fillOpacity;
    SliderField *m_tolerance;
    QSpinBox *m_fillExpand;
    QComboBox *m_fillSource;

    QLabel *m_transformHelp;
    QWidget *m_footer;
    AdvancedToolSettings *m_advanced = nullptr; // made on first use
};

} // namespace mothion::ui
