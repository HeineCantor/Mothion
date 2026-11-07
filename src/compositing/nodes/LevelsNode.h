#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// Levels: what is darker than Input black becomes black, what is lighter than Input white white, the middle bent by
// Gamma, and the result put between Output black and Output white.
class LevelsNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("levels"); }
    QString title() const override { return nodeTr("Levels"); }
    NodeCategory category() const override { return NodeCategory::Color; }
    QColor tint() const override { return {200, 130, 70}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("inBlack"), nodeTr("Input black"), 0, 254, 0),
                ParamInfo::slider(QStringLiteral("inWhite"), nodeTr("Input white"), 1, 255, 255),
                ParamInfo::slider(QStringLiteral("gamma"), nodeTr("Gamma (%)"), 10, 300, 100),
                ParamInfo::slider(QStringLiteral("outBlack"), nodeTr("Output black"), 0, 255, 0),
                ParamInfo::slider(QStringLiteral("outWhite"), nodeTr("Output white"), 0, 255, 255)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
