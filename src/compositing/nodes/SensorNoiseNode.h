#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// The grain of a camera sensor, in two parts that can be set apart: Luminance noise makes pixels lighter or
// darker, Chroma noise tints them with random color, in coarser blotches. Dark areas get more of both, as on a
// sensor. With nothing connected the node makes the noise on a mid gray. The grain is the same every time it is
// computed (it comes from the pixel's place and the Seed, not from a random generator), and changes with the
// frame only if Animate is on.
class SensorNoiseNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("sensornoise"); }
    QString title() const override { return nodeTr("Sensor Noise"); }
    NodeCategory category() const override { return NodeCategory::Effect; }
    QColor tint() const override { return {100, 105, 135}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("luminance"), nodeTr("Luminance"), 0, 100, 20),
                ParamInfo::slider(QStringLiteral("chroma"), nodeTr("Chroma"), 0, 100, 10),
                ParamInfo::slider(QStringLiteral("size"), nodeTr("Grain size"), 1, 8, 1),
                ParamInfo::slider(QStringLiteral("seed"), nodeTr("Seed"), 0, 999, 0),
                ParamInfo::toggle(QStringLiteral("animate"), nodeTr("Animate"), false)};
    }
    bool usesFrameIndex(const QVariantMap &params) const override
    {
        return params.value(QStringLiteral("animate")).toInt() != 0;
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
