#include "tools/ToolState.h"

#include <algorithm>

namespace mothion::tools {

ToolState::ToolState(QObject *parent)
    : QObject(parent)
{
    m_settings[index(ToolId::Eraser)].size = 30.0;
    m_brushes.fill(roundBrush());
}

BrushDefinitionPtr roundBrush()
{
    static const BrushDefinitionPtr round = [] {
        auto b = std::make_shared<BrushDefinition>();
        b->name = QObject::tr("Round");
        return b; // BrushStyle's defaults: hard, radius × pressure, dense spacing
    }();
    return round;
}

QString ToolState::displayName(ToolId tool)
{
    switch (tool) {
    case ToolId::Brush: return tr("Brush");
    case ToolId::Eraser: return tr("Eraser");
    case ToolId::Hand: return tr("Hand");
    case ToolId::Zoom: return tr("Zoom");
    case ToolId::Move: return tr("Move");
    case ToolId::RectSelect: return tr("Rectangle selection");
    case ToolId::LassoSelect: return tr("Lasso selection");
    case ToolId::Puppet: return tr("Puppet warp");
    case ToolId::Eyedropper: return tr("Eyedropper");
    case ToolId::Fill: return tr("Fill");
    case ToolId::Transform: return tr("Transform");
    }
    return {};
}

void ToolState::setActiveTool(ToolId tool)
{
    if (tool == m_activeTool)
        return;
    m_previousTool = m_activeTool;
    m_activeTool = tool;
    emit activeToolChanged(tool);
}

void ToolState::toggleTool(ToolId tool)
{
    setActiveTool(tool == m_activeTool ? m_previousTool : tool);
}

void ToolState::setSize(ToolId tool, double size)
{
    size = std::clamp(size, kMinSize, kMaxSize);
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

void ToolState::setBrush(ToolId tool, BrushDefinitionPtr brush)
{
    if (!brush)
        brush = roundBrush();
    auto &current = m_brushes[index(tool)];
    if (current == brush)
        return;
    current = std::move(brush);
    emit toolSettingsChanged(tool);
}

void ToolState::setColor(const QColor &color)
{
    if (!color.isValid() || color == m_color)
        return;
    m_color = color;
    emit colorChanged(color);
}

void ToolState::setSecondaryColor(const QColor &color)
{
    if (!color.isValid() || color == m_secondaryColor)
        return;
    m_secondaryColor = color;
    emit secondaryColorChanged(color);
}

void ToolState::swapColors()
{
    std::swap(m_color, m_secondaryColor);
    emit colorChanged(m_color);
    emit secondaryColorChanged(m_secondaryColor);
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

void ToolState::setUseTilt(bool use)
{
    if (use == m_useTilt)
        return;
    m_useTilt = use;
    emit useTiltChanged(use);
}

void ToolState::setSharpCorners(bool on)
{
    if (on == m_sharpCorners)
        return;
    m_sharpCorners = on;
    emit sharpCornersChanged(on);
}

void ToolState::setCorrectStartAfterStroke(bool on)
{
    if (on == m_correctStartAfterStroke)
        return;
    m_correctStartAfterStroke = on;
    emit correctStartAfterStrokeChanged(on);
}

void ToolState::setPuppetSettings(const PuppetSettings &settings)
{
    PuppetSettings s = settings;
    s.radius = std::clamp(s.radius, kMinPuppetRadius, kMaxPuppetRadius);
    if (s == m_puppet)
        return;
    m_puppet = s;
    emit puppetSettingsChanged();
}

void ToolState::setOnionSkin(const OnionSkinSettings &settings)
{
    OnionSkinSettings s = settings;
    s.before = std::clamp(s.before, 0, kMaxOnionFrames);
    s.after = std::clamp(s.after, 0, kMaxOnionFrames);
    s.opacity = std::clamp(s.opacity, 0.0, 1.0);
    if (s == m_onion)
        return;
    m_onion = s;
    emit onionSkinChanged();
}

void ToolState::setFillSettings(const FillSettings &settings)
{
    if (settings == m_fill)
        return;
    m_fill = settings;
    emit fillSettingsChanged();
}

} // namespace mothion::tools
