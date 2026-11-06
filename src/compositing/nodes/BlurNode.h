#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// A box blur of Radius pixels (of the document: the preview, being smaller, blurs less pixels).
class BlurNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("blur"); }
    QString title() const override { return nodeTr("Blur"); }
    QColor tint() const override { return {150, 90, 60}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("radius"), nodeTr("Radius"), 0, 60, 8)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
