#include "tools/BrushPreset.h"

#include "tools/BuiltInBrushes.h"

namespace mothion::tools {

namespace {

// Curve shorthands (control points of PressureCurve).
const PressureCurve kLinear;
const PressureCurve kSteady({0.0f, 0.75f}, {0.25f, 1.0f}); // nearly constant width

} // namespace

QList<BrushPreset> builtInPresets(ToolId tool)
{
    switch (tool) {
    case ToolId::Pencil:
    case ToolId::Pen:
    case ToolId::Brush:
    case ToolId::Watercolor:
    case ToolId::Airbrush:
    case ToolId::Decoration:
        return brushCategoryPresets(tool);
    case ToolId::Eraser:
    {
        QList<BrushPreset> erasers = {
            {QObject::tr("Eraser"), tool, 30.0, 1.0, kSteady, 0.1},
            {QObject::tr("Fine eraser"), tool, 6.0, 1.0, kLinear, 0.2},
            {QObject::tr("Large eraser"), tool, 120.0, 1.0, kSteady, 0.0},
        };
        const char *ids[] = {"eraser/eraser", "eraser/fine", "eraser/large"};
        for (qsizetype i = 0; i < erasers.size(); ++i)
            erasers[i].id = QLatin1String(ids[i]);
        return erasers;
    }
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
        bucket.fillAllLayers = false; // the lines of this layer only
        BrushPreset others{QObject::tr("Fill refer to other layers"), tool};
        others.fillMode = FillSettings::Mode::Bucket;
        others.fillAllLayers = true; // the union of the layers shown, merged or not
        BrushPreset lasso{QObject::tr("Lasso fill"), tool};
        lasso.fillMode = FillSettings::Mode::Lasso;
        return {bucket, others, lasso};
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
    case ToolId::Shape: {
        BrushPreset line{QObject::tr("Line"), tool};
        line.shapeKind = ShapeSettings::Kind::Line;
        BrushPreset rectangle{QObject::tr("Rectangle"), tool};
        rectangle.shapeKind = ShapeSettings::Kind::Rectangle;
        BrushPreset rounded{QObject::tr("Rounded rectangle"), tool};
        rounded.shapeKind = ShapeSettings::Kind::RoundedRectangle;
        BrushPreset ellipse{QObject::tr("Ellipse"), tool};
        ellipse.shapeKind = ShapeSettings::Kind::Ellipse;
        BrushPreset polygon{QObject::tr("Polygon"), tool};
        polygon.shapeKind = ShapeSettings::Kind::Polygon;
        BrushPreset curve{QObject::tr("Curve"), tool};
        curve.shapeKind = ShapeSettings::Kind::Curve;
        return {line, rectangle, rounded, ellipse, polygon, curve};
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
        if (preset.fillAllLayers)
            fill.allLayers = *preset.fillAllLayers;
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
    if (preset.shapeKind) {
        ShapeSettings shape = state.shapeSettings();
        shape.kind = *preset.shapeKind;
        state.setShapeSettings(shape);
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
