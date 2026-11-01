#include "ui/panels/BrushSlidersPanel.h"

#include "ui/panels/VerticalValueSlider.h"

#include <QVBoxLayout>

#include <cmath>

namespace mothion::ui {

using tools::ToolState;

BrushSlidersPanel::BrushSlidersPanel(ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_size(new VerticalValueSlider(ToolState::kMinSize, ToolState::kMaxSize,
                                     VerticalValueSlider::Scale::Logarithmic, this))
    , m_opacity(new VerticalValueSlider(0.0, 100.0, VerticalValueSlider::Scale::Linear, this))
{
    m_size->setToolTip(tr("Brush size"));
    m_size->setFormatter([](double v) {
        return QStringLiteral("%1\npx").arg(v, 0, 'f', v < 100 ? 1 : 0);
    });
    m_opacity->setToolTip(tr("Opacity"));
    m_opacity->setFormatter([](double v) { return QStringLiteral("%1\n%").arg(std::lround(v)); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(8);
    layout->addWidget(m_size, 3, Qt::AlignHCenter);
    layout->addWidget(m_opacity, 2, Qt::AlignHCenter);

    connect(m_size, &VerticalValueSlider::valueEdited, this, [this](double v) {
        m_state->setSize(m_state->activeTool(), std::round(v * 10) / 10);
    });
    connect(m_opacity, &VerticalValueSlider::valueEdited, this,
            [this](double v) { m_state->setOpacity(m_state->activeTool(), std::round(v) / 100.0); });
    connect(m_state, &ToolState::activeToolChanged, this, &BrushSlidersPanel::syncFromState);
    connect(m_state, &ToolState::toolSettingsChanged, this, &BrushSlidersPanel::syncFromState);
    syncFromState();
}

void BrushSlidersPanel::syncFromState()
{
    const bool paint = ToolState::isPaintTool(m_state->activeTool());
    m_size->setEnabled(paint);
    m_opacity->setEnabled(paint);
    const tools::BrushSettings s = m_state->settings(m_state->activeTool());
    m_size->setValue(s.size);
    m_opacity->setValue(s.opacity * 100.0);
}

} // namespace mothion::ui
