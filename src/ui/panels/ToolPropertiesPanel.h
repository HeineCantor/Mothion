#pragma once

#include "tools/ToolState.h"

#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

class AdvancedToolSettings;
class PressureCurveEditor;
class SliderField;

// Properties of the active tool: size and opacity (paint tools only),
// plus the global smoothing, pen tilt switch and pressure curve (the rarer brush
// options are in AdvancedToolSettings, behind the wrench button); for the puppet warp, its
// mesh options instead; for the fill, its area options; for the transform, the filter that reads the pixels.
class ToolPropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ToolPropertiesPanel(tools::ToolState *state, QWidget *parent = nullptr);

    PressureCurveEditor *curveEditor() const { return m_curveEditor; }
    // The document whose cels the Animate tool's options show and edit (the pose of the current cel on the current frame).
    void setDocument(document::DocumentController *document);
    // A bar with the wrench (Advanced Tool Settings), for the dock to keep
    // under the panel, outside its scrolling (see PanelDock).
    QWidget *footer() const { return m_footer; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void syncFromState();
    void syncPuppetFromState();
    void syncFillFromState();
    void syncEffectFromState();
    void syncShapeFromState();
    void syncVectorEraserFromState();
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

    // Blend and liquify: the brush's size and its strength (the tool's opacity).
    QWidget *m_effectControls;
    SliderField *m_effectSize;
    SliderField *m_effectStrength;

    // Shape: the outline's thickness, its opacity and whether it is outlined or filled.
    QWidget *m_shapeControls;
    SliderField *m_shapeThickness;
    SliderField *m_shapeOpacity;
    QComboBox *m_shapeStyle;
    SliderField *m_shapeRadius;

    // Eraser: what it does on a vector layer (tools::VectorEraserSettings).
    QWidget *m_vectorEraserControls = nullptr;
    QButtonGroup *m_vectorEraserModes = nullptr; // one button per mode, one down at a time
    // The brush's advanced options (pen tilt, pressure curve), folded away at the end of the panel.
    QToolButton *m_advancedToggle = nullptr;
    QWidget *m_advancedBody = nullptr;

    QWidget *m_footer;
    QLabel *m_transformHelp;
    // Transform: how the pixels are read when they are scaled or turned (tools::Preferences::transformFilter()).
    QWidget *m_transformControls = nullptr;
    QComboBox *m_transformFilter = nullptr;
    // Animate: the pose of the current cel on the current frame, and its key (auto-key: a field edited makes it).
    void syncAnimateFromDocument();
    document::DocumentController *m_document = nullptr;
    QWidget *m_animateControls = nullptr;
    QLabel *m_animateCel = nullptr;
    QDoubleSpinBox *m_poseX = nullptr;
    QDoubleSpinBox *m_poseY = nullptr;
    QDoubleSpinBox *m_poseRotation = nullptr;
    QDoubleSpinBox *m_poseScale = nullptr;
    QDoubleSpinBox *m_poseOpacity = nullptr;
    QComboBox *m_poseEasing = nullptr;
    QPushButton *m_poseKey = nullptr;
    QPushButton *m_poseDelete = nullptr;
    AdvancedToolSettings *m_advanced = nullptr; // made on first use
};

} // namespace mothion::ui
