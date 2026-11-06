#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// Areas of the picture, chosen by hand (rectangles and free shapes drawn on the preview while the node is selected).
// Two outputs: Image, the input with what is outside the areas made see-through, and Mask, the areas alone as a
// gray picture (white inside), which a Merge takes to show its Front only there. The areas are the node's data.
class MaskNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("mask"); }
    QString title() const override { return nodeTr("Mask"); }
    QColor tint() const override { return {125, 95, 165}; }
    QStringList inputs() const override { return {nodeTr("Image")}; }
    QStringList outputs() const override { return {nodeTr("Image"), nodeTr("Mask")}; }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
    QList<QImage> processOutputs(const QList<QImage> &inputs, const QVariantMap &params,
                                 const EvalContext &context) const override;
};

} // namespace mothion::compositing
