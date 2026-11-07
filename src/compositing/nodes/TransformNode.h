#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// Moves, scales and turns the picture around its center; what comes in from outside the frame is empty.
class TransformNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("transform"); }
    QString title() const override { return nodeTr("Transform"); }
    NodeCategory category() const override { return NodeCategory::Transform; }
    QColor tint() const override { return {70, 120, 170}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("x"), nodeTr("Move X"), -2000, 2000, 0),
                ParamInfo::slider(QStringLiteral("y"), nodeTr("Move Y"), -2000, 2000, 0),
                ParamInfo::slider(QStringLiteral("scale"), nodeTr("Scale (%)"), 1, 400, 100),
                ParamInfo::slider(QStringLiteral("rotation"), nodeTr("Rotation"), -180, 180, 0)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
