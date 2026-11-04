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
            {QObject::tr("Pencil"), tool, 6.0, 0.85, kFirm, 0.2},
            {QObject::tr("Pen"), tool, 5.0, 1.0, kLinear, 0.45},
            {QObject::tr("Nib"), tool, 9.0, 1.0, kTaper, 0.5},
            {QObject::tr("Marker"), tool, 14.0, 1.0, kSteady, 0.25},
            {QObject::tr("Brush"), tool, 32.0, 0.7, kLinear, 0.15},
            {QObject::tr("Fill"), tool, 80.0, 1.0, kSteady, 0.1},
        };
    case ToolId::Eraser:
        return {
            {QObject::tr("Eraser"), tool, 30.0, 1.0, kSteady, 0.1},
            {QObject::tr("Fine eraser"), tool, 6.0, 1.0, kLinear, 0.2},
            {QObject::tr("Large eraser"), tool, 120.0, 1.0, kSteady, 0.0},
        };
    case ToolId::RectSelect:
    case ToolId::LassoSelect: { // the selection tool's two shapes
        BrushPreset rectangle{QObject::tr("Rectangle"), tool};
        rectangle.selectsTool = ToolId::RectSelect;
        BrushPreset lasso{QObject::tr("Lasso"), tool};
        lasso.selectsTool = ToolId::LassoSelect;
        return {rectangle, lasso};
    }
    case ToolId::Fill: {
        BrushPreset bucket{QObject::tr("Fill"), tool};
        bucket.fillMode = FillSettings::Mode::Bucket;
        BrushPreset lasso{QObject::tr("Lasso fill"), tool};
        lasso.fillMode = FillSettings::Mode::Lasso;
        return {bucket, lasso};
    }
    case ToolId::Blend: {
        BrushPreset smudge{QObject::tr("Smudge"), tool};
        smudge.blendMode = BlendSettings::Mode::Smudge;
        BrushPreset blur{QObject::tr("Blur"), tool};
        blur.blendMode = BlendSettings::Mode::Blur;
        return {smudge, blur};
    }
    case ToolId::Liquify: {
        BrushPreset push{QObject::tr("Push"), tool};
        push.liquifyMode = LiquifySettings::Mode::Push;
        BrushPreset expand{QObject::tr("Expand"), tool};
        expand.liquifyMode = LiquifySettings::Mode::Expand;
        BrushPreset shrink{QObject::tr("Shrink"), tool};
        shrink.liquifyMode = LiquifySettings::Mode::Shrink;
        return {push, expand, shrink};
    }
    case ToolId::Hand:
    case ToolId::Zoom:
    case ToolId::Move:
    case ToolId::Puppet:
    case ToolId::Eyedropper:
    case ToolId::Transform:
        return {}; // no sub tools
    }
    return {};
}

void applyPreset(ToolState &state, const BrushPreset &preset)
{
    if (preset.selectsTool) {
        state.setActiveTool(*preset.selectsTool);
        return;
    }
    if (preset.fillMode) {
        FillSettings fill = state.fillSettings();
        fill.mode = *preset.fillMode;
        state.setFillSettings(fill);
        return;
    }
    if (preset.blendMode) {
        BlendSettings blend = state.blendSettings();
        blend.mode = *preset.blendMode;
        state.setBlendSettings(blend);
        return;
    }
    if (preset.liquifyMode) {
        LiquifySettings liquify = state.liquifySettings();
        liquify.mode = *preset.liquifyMode;
        state.setLiquifySettings(liquify);
        return;
    }
    state.setSize(preset.tool, preset.size);
    state.setOpacity(preset.tool, preset.opacity);
    state.setPressureCurve(preset.curve);
    state.setSmoothing(preset.smoothing);
    state.setSharpCorners(preset.sharpCorners);
    state.setBrush(preset.tool, preset.brush);
}

} // namespace mothion::tools
