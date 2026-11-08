#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// A lens's bending of the picture: a positive Amount bulges it as a wide angle or a fisheye does (the straight lines
// bow outwards, the edges are drawn in and the corners go empty), a negative one pinches it the other way. Zoom
// brings the picture back out to the edges, and the Center is the point the lens looks along.
class LensDistortionNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("lensdistortion"); }
    QString title() const override { return nodeTr("Lens distortion"); }
    NodeCategory category() const override { return NodeCategory::Transform; }
    QColor tint() const override { return {120, 90, 150}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    QList<ParamInfo> params() const override
    {
        return {ParamInfo::slider(QStringLiteral("amount"), nodeTr("Amount"), -100, 100, 40),
                ParamInfo::toggle(QStringLiteral("autosize"), nodeTr("Autosize"), false),
                ParamInfo::slider(QStringLiteral("zoom"), nodeTr("Zoom"), 50, 300, 100),
                ParamInfo::choice(QStringLiteral("quality"), nodeTr("Quality"),
                                  {nodeTr("Bilinear"), nodeTr("Bicubic"), nodeTr("Lanczos")}, 1),
                ParamInfo::slider(QStringLiteral("centerx"), nodeTr("Center X"), 0, 100, 50),
                ParamInfo::slider(QStringLiteral("centery"), nodeTr("Center Y"), 0, 100, 50)};
    }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
