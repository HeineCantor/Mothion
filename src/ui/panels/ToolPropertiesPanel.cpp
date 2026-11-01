#include "ui/panels/ToolPropertiesPanel.h"

#include "ui/panels/PressureCurveEditor.h"
#include "ui/panels/SliderField.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace mothion::ui {

using tools::ToolId;
using tools::ToolState;

ToolPropertiesPanel::ToolPropertiesPanel(ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_toolName(new QLabel(this))
    , m_size(new SliderField(tr("Size"), int(ToolState::kMinSize), int(ToolState::kMaxSize), QStringLiteral(" px"), this))
    , m_opacity(new SliderField(tr("Opacity"), 0, 100, QStringLiteral(" %"), this))
    , m_smoothing(new SliderField(tr("Stabilizer"), 0, 95, QStringLiteral(" %"), this))
    , m_curveEditor(new PressureCurveEditor(this))
{
    m_toolName->setObjectName(QStringLiteral("PanelTitle"));
    auto *resetCurve = new QPushButton(tr("Linear curve"), this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(m_toolName);
    layout->addWidget(m_size);
    layout->addWidget(m_opacity);
    layout->addWidget(m_smoothing);
    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Pressure curve"), this));
    layout->addWidget(m_curveEditor);
    layout->addWidget(resetCurve, 0, Qt::AlignRight);
    layout->addStretch(1);

    connect(m_size, &SliderField::valueEdited, this,
            [this](int v) { m_state->setSize(m_state->activeTool(), v); });
    connect(m_opacity, &SliderField::valueEdited, this,
            [this](int v) { m_state->setOpacity(m_state->activeTool(), v / 100.0); });
    connect(m_smoothing, &SliderField::valueEdited, this,
            [this](int v) { m_state->setSmoothing(v / 100.0); });
    connect(m_curveEditor, &PressureCurveEditor::curveEdited, m_state, &ToolState::setPressureCurve);
    connect(resetCurve, &QPushButton::clicked, this, [this] { m_state->setPressureCurve(PressureCurve()); });

    connect(m_state, &ToolState::activeToolChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::toolSettingsChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::smoothingChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::pressureCurveChanged, this,
            [this] { m_curveEditor->setCurve(m_state->pressureCurve()); });
    syncFromState();
    m_curveEditor->setCurve(m_state->pressureCurve());
}

void ToolPropertiesPanel::syncFromState()
{
    const ToolId tool = m_state->activeTool();
    const bool paint = ToolState::isPaintTool(tool);
    m_toolName->setText(ToolState::displayName(tool));
    m_size->setEnabled(paint);
    m_opacity->setEnabled(paint);

    const tools::BrushSettings s = m_state->settings(tool);
    m_size->setValue(int(std::lround(s.size)));
    m_opacity->setValue(int(std::lround(s.opacity * 100)));
    m_smoothing->setValue(int(std::lround(m_state->smoothing() * 100)));
}

} // namespace mothion::ui
