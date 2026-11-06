#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// An image of one color (alpha included) the size of the frame.
class ColorNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("color"); }
    QString title() const override { return nodeTr("Color"); }
    QColor tint() const override { return {140, 80, 140}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::color(QStringLiteral("color"), nodeTr("Color"), QColor(150, 60, 190, 110))};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
