#pragma once

#include "core/PressureCurve.h"
#include "tools/BrushDefinition.h"
#include "tools/ToolState.h"

#include <QList>
#include <QString>

namespace mothion::tools {

// A named set of tool settings ("sub tool"): picking one in the Sub-tool
// panel copies its values into ToolState. Built-in and in memory for now;
// user presets and saving come with the document/settings work.
struct BrushPreset
{
    QString name;
    ToolId tool = ToolId::Brush;
    double size = 10.0;
    double opacity = 1.0;
    PressureCurve curve;
    double smoothing = 0.3;
    BrushDefinitionPtr brush; // null = round brush
};

// Built-in presets for a tool (empty for tools without sub tools).
QList<BrushPreset> builtInPresets(ToolId tool);

void applyPreset(ToolState &state, const BrushPreset &preset);

} // namespace mothion::tools
