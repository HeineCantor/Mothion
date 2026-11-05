#pragma once

#include "core/PressureCurve.h"
#include "tools/BrushDefinition.h"
#include "tools/ToolState.h"

#include <QList>
#include <QString>

#include <optional>
#include <utility>

namespace mothion::tools {

// A named set of tool settings ("sub tool"): picking one in the Sub-tool
// panel copies its values into ToolState. Built-in and in memory for now;
// user presets and saving come with the document/settings work.
struct BrushPreset
{
    BrushPreset() = default;
    // A brush or eraser sub tool; everything else keeps its default.
    BrushPreset(QString presetName, ToolId forTool, double brushSize, double brushOpacity, PressureCurve pressure,
                double stabilizer)
        : name(std::move(presetName))
        , tool(forTool)
        , size(brushSize)
        , opacity(brushOpacity)
        , curve(std::move(pressure))
        , smoothing(stabilizer)
    {
    }
    // A sub tool that only sets a mode (fill, blend, liquify, shape) or picks a tool.
    BrushPreset(QString presetName, ToolId forTool)
        : name(std::move(presetName))
        , tool(forTool)
    {
    }

    QString name;
    ToolId tool = ToolId::Brush;
    double size = 10.0;
    double opacity = 1.0;
    PressureCurve curve;
    double smoothing = 0.3;
    bool sharpCorners = false; // see ToolState::sharpCorners()
    BrushDefinitionPtr brush; // null = round brush
    // A fill sub tool: sets only the fill mode, none of the brush values above.
    std::optional<FillSettings::Mode> fillMode;
    // A blend or liquify sub tool: sets only the mode of that tool.
    std::optional<BlendSettings::Mode> blendMode;
    std::optional<LiquifySettings::Mode> liquifyMode;
    // A shape sub tool: sets only the kind of shape (the style stays as it is).
    std::optional<ShapeSettings::Kind> shapeKind;
    // A sub tool that is a tool of its own (the selection shapes): picking it
    // makes that tool active, nothing else.
    std::optional<ToolId> selectsTool;
};

// Built-in presets for a tool (empty for tools without sub tools).
QList<BrushPreset> builtInPresets(ToolId tool);

void applyPreset(ToolState &state, const BrushPreset &preset);

} // namespace mothion::tools
