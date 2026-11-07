#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// The picture over its own shadow: its shape (the alpha), in a color, moved by an offset and blurred (pixels of the
// document).
class DropShadowNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("dropshadow"); }
    QString title() const override { return nodeTr("Drop Shadow"); }
    NodeCategory category() const override { return NodeCategory::Effect; }
    QColor tint() const override { return {80, 80, 100}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("x"), nodeTr("Offset X"), -100, 100, 8),
                ParamInfo::slider(QStringLiteral("y"), nodeTr("Offset Y"), -100, 100, 8),
                ParamInfo::slider(QStringLiteral("blur"), nodeTr("Blur"), 0, 60, 8),
                ParamInfo::slider(QStringLiteral("opacity"), nodeTr("Opacity"), 0, 100, 60),
                ParamInfo::color(QStringLiteral("color"), nodeTr("Color"), QColor(0, 0, 0))};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
