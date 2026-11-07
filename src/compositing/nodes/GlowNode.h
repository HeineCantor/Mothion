#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// A bloom: the bright parts of the picture spill light around them. The parts above Threshold (with a soft
// knee, Softness) are spread by a pyramid of ever coarser, blurred copies, which gives the long tail of
// light a real glow has, and added to the picture, in linear light, times Intensity and Tint. Size is the
// reach of the glow in pixels of the document. With "Ignore paper" the color of the page gives no light: only
// what is drawn on it does (the glow still falls on the page).
class GlowNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("glow"); }
    QString title() const override { return nodeTr("Glow"); }
    NodeCategory category() const override { return NodeCategory::Effect; }
    QColor tint() const override { return {190, 160, 70}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("threshold"), nodeTr("Threshold"), 0, 100, 70),
                ParamInfo::slider(QStringLiteral("softness"), nodeTr("Softness"), 0, 100, 30),
                ParamInfo::slider(QStringLiteral("size"), nodeTr("Size"), 1, 100, 20),
                ParamInfo::slider(QStringLiteral("intensity"), nodeTr("Intensity"), 0, 300, 100),
                ParamInfo::color(QStringLiteral("tint"), nodeTr("Tint"), QColor(255, 255, 255)),
                ParamInfo::toggle(QStringLiteral("ignorePaper"), nodeTr("Ignore paper"), true)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
