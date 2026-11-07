#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// The picture with its alpha multiplied by an opacity, or turned upside down (what was see-through shows, in black,
// and the other way round).
class OpacityNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("opacity"); }
    QString title() const override { return nodeTr("Opacity"); }
    NodeCategory category() const override { return NodeCategory::Composite; }
    QColor tint() const override { return {110, 110, 150}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("opacity"), nodeTr("Opacity"), 0, 100, 100),
                ParamInfo::toggle(QStringLiteral("invert"), nodeTr("Invert alpha"), false)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
