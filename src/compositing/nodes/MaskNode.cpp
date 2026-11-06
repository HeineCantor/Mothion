#include "compositing/nodes/MaskNode.h"

#include "compositing/MaskData.h"

namespace mothion::compositing {

QList<QImage> MaskNode::processOutputs(const QList<QImage> &inputs, const QVariantMap &params,
                                       const EvalContext &context) const
{
    const QImage &input = inputs.value(0);
    // With nothing connected the mask still exists, the size of the frame.
    const QSize size = input.isNull() ? context.frame.size() : input.size();
    if (size.isEmpty())
        return {QImage(), QImage()};
    const QImage mask = rasterizeMask(parseMaskShapes(params.value(kDataKey).toString()), size, context.scale);
    return {input.isNull() ? QImage() : applyMask(input, mask), mask};
}

QImage MaskNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &context) const
{
    return processOutputs(inputs, params, context).value(0);
}

} // namespace mothion::compositing
