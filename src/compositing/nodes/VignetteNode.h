#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// The corners go towards a color (black), from a distance from the center (Size, as a part of the way to the
// corner) on, gently as Softness says, as much as Amount.
class VignetteNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("vignette"); }
    QString title() const override { return nodeTr("Vignette"); }
    NodeCategory category() const override { return NodeCategory::Effect; }
    QColor tint() const override { return {90, 90, 130}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("amount"), nodeTr("Amount"), 0, 100, 50),
                ParamInfo::slider(QStringLiteral("size"), nodeTr("Size"), 0, 100, 55),
                ParamInfo::slider(QStringLiteral("softness"), nodeTr("Softness"), 0, 100, 60),
                ParamInfo::color(QStringLiteral("tint"), nodeTr("Color"), QColor(0, 0, 0))};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
