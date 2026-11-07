#pragma once

#include "compositing/NodeType.h"

namespace mothion::compositing {

// The frame being shown, as the visible layers compose it.
class FrameNode : public NodeType
{
public:
    QString id() const override { return QStringLiteral("frame"); }
    QString title() const override { return nodeTr("Frame"); }
    NodeCategory category() const override { return NodeCategory::InputOutput; }
    QColor tint() const override { return {70, 110, 170}; }
    QStringList outputs() const override { return {nodeTr("Image")}; }
    bool dependsOnFrame() const override { return true; }
    QImage process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const override;
};

} // namespace mothion::compositing
