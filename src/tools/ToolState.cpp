#include "tools/ToolState.h"

#include <algorithm>

namespace mothion::tools {

ToolState::ToolState(QObject *parent)
    : QObject(parent)
{
    m_settings[index(ToolId::Eraser)].size = 30.0;
}

QString ToolState::displayName(ToolId tool)
{
    switch (tool) {
    case ToolId::Brush: return tr("Pennello");
    case ToolId::Eraser: return tr("Gomma");
    case ToolId::Hand: return tr("Mano");
    case ToolId::Zoom: return tr("Zoom");
    }
    return {};
}

void ToolState::setActiveTool(ToolId tool)
{
    if (tool == m_activeTool)
        return;
    m_activeTool = tool;
    emit activeToolChanged(tool);
}

void ToolState::setSize(ToolId tool, double size)
{
    size = std::clamp(size, 1.0, 500.0);
    auto &s = m_settings[index(tool)];
    if (qFuzzyCompare(s.size, size))
        return;
    s.size = size;
    emit toolSettingsChanged(tool);
}

void ToolState::setOpacity(ToolId tool, double opacity)
{
    opacity = std::clamp(opacity, 0.0, 1.0);
    auto &s = m_settings[index(tool)];
    if (qFuzzyCompare(s.opacity, opacity))
        return;
    s.opacity = opacity;
    emit toolSettingsChanged(tool);
}

void ToolState::setColor(const QColor &color)
{
    if (!color.isValid() || color == m_color)
        return;
    m_color = color;
    emit colorChanged(color);
}

void ToolState::setPressureCurve(const PressureCurve &curve)
{
    if (curve == m_pressureCurve)
        return;
    m_pressureCurve = curve;
    emit pressureCurveChanged();
}

void ToolState::setSmoothing(double amount)
{
    amount = std::clamp(amount, 0.0, 0.95);
    if (qFuzzyCompare(1.0 + amount, 1.0 + m_smoothing))
        return;
    m_smoothing = amount;
    emit smoothingChanged(amount);
}

} // namespace mothion::tools
