#include "tools/BrushPreset.h"

namespace mothion::tools {

namespace {

// Curve shorthands (control points of PressureCurve).
const PressureCurve kLinear;
const PressureCurve kFirm({0.5f, 0.05f}, {0.9f, 0.6f});  // needs pressure to get wide
const PressureCurve kTaper({0.65f, 0.0f}, {0.85f, 0.9f}); // thin hairlines, strong swell
const PressureCurve kSteady({0.0f, 0.75f}, {0.25f, 1.0f}); // nearly constant width

} // namespace

QList<BrushPreset> builtInPresets(ToolId tool)
{
    switch (tool) {
    case ToolId::Brush:
        return {
            {QObject::tr("Matita"), tool, 6.0, 0.85, kFirm, 0.2},
            {QObject::tr("Penna"), tool, 5.0, 1.0, kLinear, 0.45},
            {QObject::tr("Pennino"), tool, 9.0, 1.0, kTaper, 0.5},
            {QObject::tr("Pennarello"), tool, 14.0, 1.0, kSteady, 0.25},
            {QObject::tr("Pennello"), tool, 32.0, 0.7, kLinear, 0.15},
            {QObject::tr("Riempitivo"), tool, 80.0, 1.0, kSteady, 0.1},
        };
    case ToolId::Eraser:
        return {
            {QObject::tr("Gomma"), tool, 30.0, 1.0, kSteady, 0.1},
            {QObject::tr("Gomma fine"), tool, 6.0, 1.0, kLinear, 0.2},
            {QObject::tr("Gomma grande"), tool, 120.0, 1.0, kSteady, 0.0},
        };
    case ToolId::Hand:
    case ToolId::Zoom:
        return {};
    }
    return {};
}

void applyPreset(ToolState &state, const BrushPreset &preset)
{
    state.setSize(preset.tool, preset.size);
    state.setOpacity(preset.tool, preset.opacity);
    state.setPressureCurve(preset.curve);
    state.setSmoothing(preset.smoothing);
}

} // namespace mothion::tools
