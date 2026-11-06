#include "compositing/nodes/ColorNode.h"

namespace mothion::compositing {

QImage ColorNode::process(const QList<QImage> &, const QVariantMap &params, const EvalContext &context) const
{
    QImage solid(context.frame.size(), QImage::Format_ARGB32_Premultiplied);
    solid.fill(params.value(QStringLiteral("color")).value<QColor>());
    return solid;
}

} // namespace mothion::compositing
