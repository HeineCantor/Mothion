#include "compositing/nodes/BlurNode.h"

#include "compositing/ImageOps.h"

#include <algorithm>
#include <cmath>

namespace mothion::compositing {

QImage BlurNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const
{
    const QImage &image = inputs.value(0);
    const int radius = static_cast<int>(std::lround(params.value(QStringLiteral("radius")).toInt() * context.scale));
    return image.isNull() ? QImage() : boxBlur(image, radius);
}

} // namespace mothion::compositing
