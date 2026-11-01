#include "ui/panels/ToolPropertiesPanel.h"

#include "ui/panels/PressureCurveEditor.h"
#include "ui/panels/SliderField.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace mothion::ui {

using tools::ToolId;
using tools::ToolState;

ToolPropertiesPanel::ToolPropertiesPanel(ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_toolName(new QLabel(this))
    , m_brushControls(new QWidget(this))
    , m_size(new SliderField(tr("Size"), int(ToolState::kMinSize), int(ToolState::kMaxSize), QStringLiteral(" px"),
                             m_brushControls))
    , m_opacity(new SliderField(tr("Opacity"), 0, 100, QStringLiteral(" %"), m_brushControls))
    , m_smoothing(new SliderField(tr("Stabilizer"), 0, 95, QStringLiteral(" %"), m_brushControls))
    , m_curveEditor(new PressureCurveEditor(m_brushControls))
    , m_puppetControls(new QWidget(this))
    , m_meshDensity(new QComboBox(m_puppetControls))
    , m_meshExpand(new QSpinBox(m_puppetControls))
    , m_showMesh(new QCheckBox(tr("Show mesh"), m_puppetControls))
{
    m_toolName->setObjectName(QStringLiteral("PanelTitle"));
    auto *resetCurve = new QPushButton(tr("Linear curve"), m_brushControls);

    auto *brush = new QVBoxLayout(m_brushControls);
    brush->setContentsMargins(0, 0, 0, 0);
    brush->setSpacing(4);
    brush->addWidget(m_size);
    brush->addWidget(m_opacity);
    brush->addWidget(m_smoothing);
    brush->addSpacing(6);
    brush->addWidget(new QLabel(tr("Pressure curve"), m_brushControls));
    brush->addWidget(m_curveEditor, 1); // takes the panel's spare height
    brush->addWidget(resetCurve, 0, Qt::AlignRight);

    m_meshDensity->addItems({tr("Coarse"), tr("Normal"), tr("Fine")});
    m_meshExpand->setRange(0, 50);
    m_meshExpand->setSuffix(QStringLiteral(" px"));
    auto *help = new QLabel(tr("Click the drawing to add pins, drag a pin to deform.\n"
                               "Alt+click or Delete removes a pin.\n"
                               "Enter applies, Esc cancels."),
                            m_puppetControls);
    help->setWordWrap(true);
    help->setEnabled(false); // dimmed, as a hint
    auto *puppet = new QFormLayout(m_puppetControls);
    puppet->setContentsMargins(0, 0, 0, 0);
    puppet->addRow(tr("Mesh density"), m_meshDensity);
    puppet->addRow(tr("Mesh expansion"), m_meshExpand);
    puppet->addRow(m_showMesh);
    puppet->addRow(help);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(m_toolName);
    layout->addWidget(m_brushControls, 1);
    layout->addWidget(m_puppetControls);
    layout->addStretch(0);

    connect(m_size, &SliderField::valueEdited, this,
            [this](int v) { m_state->setSize(m_state->activeTool(), v); });
    connect(m_opacity, &SliderField::valueEdited, this,
            [this](int v) { m_state->setOpacity(m_state->activeTool(), v / 100.0); });
    connect(m_smoothing, &SliderField::valueEdited, this,
            [this](int v) { m_state->setSmoothing(v / 100.0); });
    connect(m_curveEditor, &PressureCurveEditor::curveEdited, m_state, &ToolState::setPressureCurve);
    connect(resetCurve, &QPushButton::clicked, this, [this] { m_state->setPressureCurve(PressureCurve()); });

    auto editPuppet = [this](auto change) {
        tools::PuppetSettings s = m_state->puppetSettings();
        change(s);
        m_state->setPuppetSettings(s);
    };
    connect(m_meshDensity, &QComboBox::currentIndexChanged, this,
            [editPuppet](int i) { editPuppet([i](tools::PuppetSettings &s) { s.density = i; }); });
    connect(m_meshExpand, &QSpinBox::valueChanged, this,
            [editPuppet](int v) { editPuppet([v](tools::PuppetSettings &s) { s.expand = v; }); });
    connect(m_showMesh, &QCheckBox::toggled, this,
            [editPuppet](bool on) { editPuppet([on](tools::PuppetSettings &s) { s.showMesh = on; }); });

    connect(m_state, &ToolState::activeToolChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::toolSettingsChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::smoothingChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::puppetSettingsChanged, this, &ToolPropertiesPanel::syncPuppetFromState);
    connect(m_state, &ToolState::pressureCurveChanged, this,
            [this] { m_curveEditor->setCurve(m_state->pressureCurve()); });
    syncFromState();
    syncPuppetFromState();
    m_curveEditor->setCurve(m_state->pressureCurve());
}

void ToolPropertiesPanel::syncFromState()
{
    const ToolId tool = m_state->activeTool();
    const bool paint = ToolState::isPaintTool(tool);
    const bool puppet = tool == ToolId::Puppet;
    m_toolName->setText(ToolState::displayName(tool));
    m_brushControls->setVisible(!puppet);
    m_puppetControls->setVisible(puppet);
    m_size->setEnabled(paint);
    m_opacity->setEnabled(paint);

    const tools::BrushSettings s = m_state->settings(tool);
    m_size->setValue(int(std::lround(s.size)));
    m_opacity->setValue(int(std::lround(s.opacity * 100)));
    m_smoothing->setValue(int(std::lround(m_state->smoothing() * 100)));
}

void ToolPropertiesPanel::syncPuppetFromState()
{
    const tools::PuppetSettings s = m_state->puppetSettings();
    const QSignalBlocker b1(m_meshDensity), b2(m_meshExpand), b3(m_showMesh);
    m_meshDensity->setCurrentIndex(s.density);
    m_meshExpand->setValue(s.expand);
    m_showMesh->setChecked(s.showMesh);
}

} // namespace mothion::ui
