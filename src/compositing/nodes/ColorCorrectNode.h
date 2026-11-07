#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// Brightness, contrast, gamma, hue and saturation of the picture (its alpha stays as it is).
class ColorCorrectNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("colorcorrect"); }
    QString title() const override { return nodeTr("Color Correct"); }
    NodeCategory category() const override { return NodeCategory::Color; }
    QColor tint() const override { return {190, 150, 60}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("brightness"), nodeTr("Brightness"), -100, 100, 0),
                ParamInfo::slider(QStringLiteral("contrast"), nodeTr("Contrast"), -100, 100, 0),
                ParamInfo::slider(QStringLiteral("gamma"), nodeTr("Gamma (%)"), 10, 300, 100),
                ParamInfo::slider(QStringLiteral("hue"), nodeTr("Hue"), -180, 180, 0),
                ParamInfo::slider(QStringLiteral("saturation"), nodeTr("Saturation"), -100, 100, 0)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
