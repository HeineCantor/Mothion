#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// Unsharp mask: the picture plus Amount of what it has over its own blur of Radius pixels (of the document).
class SharpenNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("sharpen"); }
    QString title() const override { return nodeTr("Sharpen"); }
    NodeCategory category() const override { return NodeCategory::Filter; }
    QColor tint() const override { return {120, 150, 90}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("amount"), nodeTr("Amount"), 0, 300, 100),
                ParamInfo::slider(QStringLiteral("radius"), nodeTr("Radius"), 1, 20, 2)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
