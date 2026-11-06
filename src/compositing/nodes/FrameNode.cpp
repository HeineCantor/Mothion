#include "compositing/nodes/FrameNode.h"

namespace mothion::compositing {

QImage FrameNode::process(const QList<QImage> &, const QVariantMap &, const EvalContext &context) const
{
    return context.frame;
}

} // namespace mothion::compositing
