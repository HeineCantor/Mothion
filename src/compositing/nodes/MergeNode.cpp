#include "compositing/nodes/MergeNode.h"

#include <QPainter>

namespace mothion::compositing {

QImage MergeNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &) const
{
    const QImage &back = inputs.value(0);
    const QImage &front = inputs.value(1);
    if (back.isNull())
        return front;
    QImage result = back.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!front.isNull()) {
        QPainter p(&result);
        p.setOpacity(params.value(QStringLiteral("opacity")).toInt() / 100.0);
        p.drawImage(QRect(QPoint(), result.size()), front);
    }
    return result;
}

} // namespace mothion::compositing
