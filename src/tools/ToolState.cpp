#include "tools/ToolState.h"

#include <algorithm>

namespace mothion::tools {

ToolState::ToolState(QObject *parent)
    : QObject(parent)
{
    m_settings[index(ToolId::Eraser)].size = 30.0;
    // For the blend and liquify brushes the opacity is their strength.
    m_settings[index(ToolId::Blend)] = {40.0, 0.6};
    m_settings[index(ToolId::Liquify)] = {90.0, 0.5};
    m_settings[index(ToolId::Shape)].size = 4.0; // the outline's thickness
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
    case ToolId::Blend: return tr("Blend");
    case ToolId::Liquify: return tr("Liquify");
    case ToolId::Shape: return tr("Shape");
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

void ToolState::setTransparentSelected(bool on)
{
    if (on == m_transparentSelected)
        return;
    m_transparentSelected = on;
    emit transparentSelectedChanged(on);
}

void ToolState::setColor(const QColor &color)
{
    if (!color.isValid())
        return;
    setTransparentSelected(false); // choosing a color is leaving the transparent one
    QColor &target = m_secondarySelected ? m_secondaryColor : m_color;
    if (color == target)
        return;
    target = color;
    emit colorChanged(color);
    if (m_secondarySelected)
        emit secondaryColorChanged(color);
}

void ToolState::setSecondaryColor(const QColor &color)
{
    if (!color.isValid() || color == m_secondaryColor)
        return;
    m_secondaryColor = color;
    emit secondaryColorChanged(color);
    if (m_secondarySelected)
        emit colorChanged(color);
}

void ToolState::selectSecondary(bool secondary)
{
    const bool wasTransparent = m_transparentSelected;
    setTransparentSelected(false);
    if (secondary == m_secondarySelected) {
        if (wasTransparent)
            emit colorChanged(color()); // back on a color
        return;
    }
    m_secondarySelected = secondary;
    emit selectedColorChanged(secondary);
    emit colorChanged(color());
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

void ToolState::setBlendSettings(const BlendSettings &settings)
{
    if (settings == m_blend)
        return;
    m_blend = settings;
    emit blendSettingsChanged();
}

void ToolState::setLiquifySettings(const LiquifySettings &settings)
{
    if (settings == m_liquify)
        return;
    m_liquify = settings;
    emit liquifySettingsChanged();
}

void ToolState::setPerspectiveSnap(bool on)
{
    if (on == m_perspectiveSnap)
        return;
    m_perspectiveSnap = on;
    emit perspectiveSnapChanged();
}

void ToolState::setPerspectiveSnapLayer(doc::LayerId layer)
{
    if (layer == m_snapLayer)
        return;
    m_snapLayer = layer;
    emit perspectiveSnapChanged();
}

void ToolState::setShapeSettings(const ShapeSettings &settings)
{
    if (settings == m_shape)
        return;
    m_shape = settings;
    emit shapeSettingsChanged();
}

} // namespace mothion::tools
