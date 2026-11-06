#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// Front over Back, with the opacity of Front.
class MergeNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("merge"); }
    QString title() const override { return nodeTr("Merge"); }
    QColor tint() const override { return {60, 130, 100}; }
    QStringList inputs() const override { return {nodeTr("Back"), nodeTr("Front")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("opacity"), nodeTr("Opacity"), 0, 100, 100)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
