#include "compositing/nodes/TransformNode.h"

#include <QPainter>
#include <QTransform>

namespace mothion::compositing {

QImage TransformNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const
{
    if (inputs.value(0).isNull())
        return {};
    const QImage source = inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const double x = params.value(QStringLiteral("x")).toInt() * context.scale; // in pixels of the document
    const double y = params.value(QStringLiteral("y")).toInt() * context.scale;
    const double scale = params.value(QStringLiteral("scale")).toInt() / 100.0;
    const double rotation = params.value(QStringLiteral("rotation")).toInt();
    if (x == 0.0 && y == 0.0 && scale == 1.0 && rotation == 0.0)
        return source;

    QImage result(source.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    const double cx = source.width() / 2.0, cy = source.height() / 2.0;
    QTransform transform;
    transform.translate(cx + x, cy + y); // where the center goes
    transform.rotate(rotation);
    transform.scale(scale, scale);
    transform.translate(-cx, -cy);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setTransform(transform);
    p.drawImage(0, 0, source);
    return result;
}

} // namespace mothion::compositing
