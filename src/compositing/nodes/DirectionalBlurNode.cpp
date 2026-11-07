#include "compositing/nodes/DirectionalBlurNode.h"

#include "compositing/ImageOps.h"

namespace mothion::compositing {

QImage DirectionalBlurNode::process(const QList<QImage> &inputs, const QVariantMap &params,
                                    const EvalContext &context) const
{
    if (inputs.value(0).isNull())
        return {};
    return directionalBlur(inputs.value(0), params.value(QStringLiteral("length")).toInt() * context.scale,
                           params.value(QStringLiteral("angle")).toInt());
}

} // namespace mothion::compositing
