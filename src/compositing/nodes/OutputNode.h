#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// The end of the graph: what reaches it is the result.
class OutputNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("output"); }
    QString title() const override { return nodeTr("Output"); }
    NodeCategory category() const override { return NodeCategory::InputOutput; }
    QColor tint() const override { return {150, 70, 80}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
