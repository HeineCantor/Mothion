#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// Front over Back, in a blend mode (the same ones the layers have: the index of the mode is the one of doc::BlendMode),
// with the opacity of Front, and, if a Mask is connected, only where the mask is.
class MergeNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("merge"); }
    QString title() const override { return nodeTr("Merge"); }
    NodeCategory category() const override { return NodeCategory::Composite; }
    QColor tint() const override { return {60, 130, 100}; }
    QStringList inputs() const override { return {nodeTr("Back"), nodeTr("Front"), nodeTr("Mask")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override;
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
