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
    , m_useTilt(new QCheckBox(tr("Use pen tilt"), m_brushControls))
    , m_curveEditor(new PressureCurveEditor(m_brushControls))
    , m_puppetControls(new QWidget(this))
    , m_meshDensity(new QComboBox(m_puppetControls))
    , m_meshExpand(new QSpinBox(m_puppetControls))
    , m_showMesh(new QCheckBox(tr("Show mesh"), m_puppetControls))
    , m_fillControls(new QWidget(this))
    , m_fillOpacity(new SliderField(tr("Opacity"), 0, 100, QStringLiteral(" %"), m_fillControls))
    , m_tolerance(new SliderField(tr("Tolerance"), 0, 100, QStringLiteral(" %"), m_fillControls))
    , m_fillExpand(new QSpinBox(m_fillControls))
    , m_fillSource(new QComboBox(m_fillControls))
    , m_transformHelp(new QLabel(tr("Drag a corner to scale (Shift keeps the proportions), a side to stretch,\n"
                                    "inside the box to move, outside it to rotate (Shift: 15° steps).\n"
                                    "Enter applies, Esc cancels."),
                                 this))
{
    m_toolName->setObjectName(QStringLiteral("PanelTitle"));
    auto *resetCurve = new QPushButton(tr("Linear curve"), m_brushControls);

    auto *brush = new QVBoxLayout(m_brushControls);
    brush->setContentsMargins(0, 0, 0, 0);
    brush->setSpacing(4);
    brush->addWidget(m_size);
    brush->addWidget(m_opacity);
    brush->addWidget(m_smoothing);
    m_useTilt->setToolTip(tr("Off: the pen's tilt changes nothing, whatever the brush's tilt dynamics say"));
    brush->addWidget(m_useTilt);
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

    m_fillExpand->setRange(0, 20);
    m_fillExpand->setSuffix(QStringLiteral(" px"));
    m_fillExpand->setToolTip(tr("How far the fill grows under the lines around it"));
    m_fillSource->addItems({tr("All layers"), tr("This layer")});
    m_fillSource->setToolTip(tr("Where the lines that stop the fill are looked for"));
    m_tolerance->setToolTip(tr("How different a color may be from the clicked one and still be filled"));
    auto *fill = new QFormLayout(m_fillControls);
    fill->setContentsMargins(0, 0, 0, 0);
    fill->addRow(m_fillOpacity);
    fill->addRow(m_tolerance);
    fill->addRow(tr("Expand"), m_fillExpand);
    fill->addRow(tr("Refer to"), m_fillSource);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(m_toolName);
    layout->addWidget(m_brushControls, 1);
    layout->addWidget(m_puppetControls);
    layout->addWidget(m_fillControls);
    m_transformHelp->setWordWrap(true);
    m_transformHelp->setEnabled(false); // dimmed, as a hint
    layout->addWidget(m_transformHelp);
    layout->addStretch(0);

    connect(m_size, &SliderField::valueEdited, this,
            [this](int v) { m_state->setSize(m_state->activeTool(), v); });
    connect(m_opacity, &SliderField::valueEdited, this,
            [this](int v) { m_state->setOpacity(m_state->activeTool(), v / 100.0); });
    connect(m_smoothing, &SliderField::valueEdited, this,
            [this](int v) { m_state->setSmoothing(v / 100.0); });
    connect(m_useTilt, &QCheckBox::toggled, m_state, &ToolState::setUseTilt);
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

    auto editFill = [this](auto change) {
        tools::FillSettings s = m_state->fillSettings();
        change(s);
        m_state->setFillSettings(s);
    };
    connect(m_fillOpacity, &SliderField::valueEdited, this,
            [this](int v) { m_state->setOpacity(ToolId::Fill, v / 100.0); });
    // The tolerance is shown in % of the 0..255 channel range.
    connect(m_tolerance, &SliderField::valueEdited, this, [editFill](int v) {
        editFill([v](tools::FillSettings &s) { s.tolerance = int(std::lround(v * 2.55)); });
    });
    connect(m_fillExpand, &QSpinBox::valueChanged, this,
            [editFill](int v) { editFill([v](tools::FillSettings &s) { s.expand = v; }); });
    connect(m_fillSource, &QComboBox::currentIndexChanged, this,
            [editFill](int i) { editFill([i](tools::FillSettings &s) { s.allLayers = i == 0; }); });
    connect(m_state, &ToolState::fillSettingsChanged, this, &ToolPropertiesPanel::syncFillFromState);
    connect(m_state, &ToolState::toolSettingsChanged, this, &ToolPropertiesPanel::syncFillFromState);
    syncFillFromState();

    connect(m_state, &ToolState::activeToolChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::toolSettingsChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::smoothingChanged, this, &ToolPropertiesPanel::syncFromState);
    connect(m_state, &ToolState::useTiltChanged, this, &ToolPropertiesPanel::syncFromState);
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
    const bool fill = tool == ToolId::Fill;
    m_toolName->setText(ToolState::displayName(tool));
    const bool transform = tool == ToolId::Transform;
    m_brushControls->setVisible(!puppet && !fill && !transform);
    m_transformHelp->setVisible(transform);
    m_puppetControls->setVisible(puppet);
    m_fillControls->setVisible(fill);
    m_size->setEnabled(paint);
    m_opacity->setEnabled(paint);

    const tools::BrushSettings s = m_state->settings(tool);
    m_size->setValue(int(std::lround(s.size)));
    m_opacity->setValue(int(std::lround(s.opacity * 100)));
    m_smoothing->setValue(int(std::lround(m_state->smoothing() * 100)));
    const QSignalBlocker block(m_useTilt);
    m_useTilt->setChecked(m_state->usesTilt());
}

void ToolPropertiesPanel::syncPuppetFromState()
{
    const tools::PuppetSettings s = m_state->puppetSettings();
    const QSignalBlocker b1(m_meshDensity), b2(m_meshExpand), b3(m_showMesh);
    m_meshDensity->setCurrentIndex(s.density);
    m_meshExpand->setValue(s.expand);
    m_showMesh->setChecked(s.showMesh);
}

void ToolPropertiesPanel::syncFillFromState()
{
    const tools::FillSettings s = m_state->fillSettings();
    const QSignalBlocker b1(m_fillExpand), b2(m_fillSource);
    m_fillOpacity->setValue(int(std::lround(m_state->settings(ToolId::Fill).opacity * 100)));
    m_tolerance->setValue(int(std::lround(s.tolerance / 2.55)));
    m_fillExpand->setValue(s.expand);
    m_fillSource->setCurrentIndex(s.allLayers ? 0 : 1);
}

} // namespace mothion::ui
