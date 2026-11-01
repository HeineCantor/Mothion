#pragma once

#include "core/PressureCurve.h"
#include "tools/BrushDefinition.h"

#include <QColor>
#include <QObject>
#include <QString>

#include <array>

namespace mothion::tools {

// The tools in the tool palette. Not to be confused with mothion::InputTool,
// which is the *physical* end of the pen (tip / eraser / mouse).
enum class ToolId { Brush, Eraser, Hand, Zoom, Move, RectSelect, LassoSelect, Puppet };
inline constexpr int kToolCount = 8;

// Puppet warp options (see puppet::PuppetRig).
struct PuppetSettings
{
    int density = 1;      // mesh: 0 coarse, 1 normal, 2 fine
    int expand = 3;       // px of mesh around the drawing
    bool showMesh = true; // draw the mesh over the canvas

    bool operator==(const PuppetSettings &) const = default;
};

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

    static constexpr double kMinSize = 1.0;   // brush diameter limits, document px
    static constexpr double kMaxSize = 2000.0;

    static bool isPaintTool(ToolId tool) { return tool == ToolId::Brush || tool == ToolId::Eraser; }
    static QString displayName(ToolId tool);

    ToolId activeTool() const { return m_activeTool; }
    void setActiveTool(ToolId tool);
    // The tool active before the current one.
    ToolId previousTool() const { return m_previousTool; }
    // Selects `tool`, or, if it is already active, goes back to the previous
    // tool (the eraser shortcut: E to erase, E again to draw on).
    void toggleTool(ToolId tool);

    BrushSettings settings(ToolId tool) const { return m_settings[index(tool)]; }
    void setSize(ToolId tool, double size);
    void setOpacity(ToolId tool, double opacity);

    // The brush a paint tool draws with (never null; round by default).
    BrushDefinitionPtr brush(ToolId tool) const { return m_brushes[index(tool)]; }
    void setBrush(ToolId tool, BrushDefinitionPtr brush);

    // Main color (what the brush paints) and secondary color; swapColors() exchanges them.
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    QColor secondaryColor() const { return m_secondaryColor; }
    void setSecondaryColor(const QColor &color);
    void swapColors();

    const PressureCurve &pressureCurve() const { return m_pressureCurve; }
    void setPressureCurve(const PressureCurve &curve);

    // 0 = raw input, towards 1 = heavier smoothing (more lag).
    double smoothing() const { return m_smoothing; }
    void setSmoothing(double amount);

    // Off: the pen's tilt is ignored (dynamics and pen direction), for pens
    // that report it badly or brushes that react to it too much.
    bool usesTilt() const { return m_useTilt; }
    void setUseTilt(bool use);

    PuppetSettings puppetSettings() const { return m_puppet; }
    void setPuppetSettings(const PuppetSettings &settings);

signals:
    void activeToolChanged(mothion::tools::ToolId tool);
    void toolSettingsChanged(mothion::tools::ToolId tool);
    void colorChanged(const QColor &color);
    void secondaryColorChanged(const QColor &color);
    void pressureCurveChanged();
    void smoothingChanged(double amount);
    void useTiltChanged(bool use);
    void puppetSettingsChanged();

private:
    static std::size_t index(ToolId tool) { return static_cast<std::size_t>(tool); }

    ToolId m_activeTool = ToolId::Brush;
    ToolId m_previousTool = ToolId::Brush;
    std::array<BrushSettings, kToolCount> m_settings;
    std::array<BrushDefinitionPtr, kToolCount> m_brushes;
    QColor m_color = QColor(0x20, 0x20, 0x24);
    QColor m_secondaryColor = Qt::white;
    PressureCurve m_pressureCurve;
    double m_smoothing = 0.3;
    bool m_useTilt = true;
    PuppetSettings m_puppet;
};

} // namespace mothion::tools
