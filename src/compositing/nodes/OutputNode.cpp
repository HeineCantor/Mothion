#include "compositing/nodes/OutputNode.h"

namespace mothion::compositing {

QImage OutputNode::process(const QList<QImage> &inputs, const QVariantMap &, const EvalContext &) const
{
    return inputs.value(0);
}

} // namespace mothion::compositing
