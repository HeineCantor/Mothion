#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// A smear along a direction, Length pixels long (of the document), for the speed of a movement.
class DirectionalBlurNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("directionalblur"); }
    QString title() const override { return nodeTr("Directional Blur"); }
    NodeCategory category() const override { return NodeCategory::Filter; }
    QColor tint() const override { return {170, 110, 70}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("length"), nodeTr("Length"), 0, 100, 12),
                ParamInfo::slider(QStringLiteral("angle"), nodeTr("Angle"), 0, 359, 0)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
