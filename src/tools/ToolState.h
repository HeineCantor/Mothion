#pragma once

#include "core/PressureCurve.h"

#include <QColor>
#include <QObject>
#include <QString>

#include <array>

namespace mothion::tools {

// The tools in the tool palette. Not to be confused with mothion::InputTool,
// which is the *physical* end of the pen (tip / eraser / mouse).
enum class ToolId { Brush, Eraser, Hand, Zoom };
inline constexpr int kToolCount = 4;

struct BrushSettings
{
    double size = 12.0;   // diameter in document pixels at full pressure
    double opacity = 1.0; // 0..1, applied to the whole stroke
};

// Shared "what will the pen do" state.
//
// Panels edit it, the canvas reads it; neither knows about the other.
// This is the only coupling point between the tool UI and the canvas.
class ToolState : public QObject
{
    Q_OBJECT

public:
    explicit ToolState(QObject *parent = nullptr);

    static bool isPaintTool(ToolId tool) { return tool == ToolId::Brush || tool == ToolId::Eraser; }
    static QString displayName(ToolId tool);

    ToolId activeTool() const { return m_activeTool; }
    void setActiveTool(ToolId tool);

    BrushSettings settings(ToolId tool) const { return m_settings[index(tool)]; }
    void setSize(ToolId tool, double size);
    void setOpacity(ToolId tool, double opacity);

    QColor color() const { return m_color; }
    void setColor(const QColor &color);

    const PressureCurve &pressureCurve() const { return m_pressureCurve; }
    void setPressureCurve(const PressureCurve &curve);

    // 0 = raw input, towards 1 = heavier smoothing (more lag).
    double smoothing() const { return m_smoothing; }
    void setSmoothing(double amount);

signals:
    void activeToolChanged(mothion::tools::ToolId tool);
    void toolSettingsChanged(mothion::tools::ToolId tool);
    void colorChanged(const QColor &color);
    void pressureCurveChanged();
    void smoothingChanged(double amount);

private:
    static std::size_t index(ToolId tool) { return static_cast<std::size_t>(tool); }

    ToolId m_activeTool = ToolId::Brush;
    std::array<BrushSettings, kToolCount> m_settings;
    QColor m_color = QColor(0x20, 0x20, 0x24);
    PressureCurve m_pressureCurve;
    double m_smoothing = 0.3;
};

} // namespace mothion::tools
