#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// The fringes of color a lens leaves at contrasty edges: the red and the blue channel are taken from places a few
// pixels apart, in opposite directions, the green stays. Radial: the shift grows from nothing at the center to
// Amount at the corners. Linear: Amount everywhere, along Angle.
class ChromaticAberrationNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("chromatic"); }
    QString title() const override { return nodeTr("Chromatic Aberration"); }
    NodeCategory category() const override { return NodeCategory::Effect; }
    QColor tint() const override { return {170, 120, 50}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::choice(QStringLiteral("mode"), nodeTr("Mode"), {nodeTr("Radial"), nodeTr("Linear")}),
                ParamInfo::slider(QStringLiteral("amount"), nodeTr("Amount"), 0, 40, 4),
                ParamInfo::slider(QStringLiteral("angle"), nodeTr("Angle"), 0, 359, 0)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
