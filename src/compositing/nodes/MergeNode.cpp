#include "compositing/nodes/MergeNode.h"

#include "canvas/BlendDraw.h"
#include "compositing/MaskData.h"
#include "core/document/Drawing.h"
#include "ui/panels/BlendModes.h"

#include <algorithm>

namespace mothion::compositing {

QList<ParamInfo> MergeNode::params() const
{
    // The modes by their number in doc::BlendMode (what is stored in files), all of them, in that order.
    QStringList modes;
    for (int i = 0; i <= int(doc::BlendMode::AddGlow); ++i)
        modes << ui::blendModeName(doc::BlendMode(i));
    return {ParamInfo::choice(QStringLiteral("mode"), nodeTr("Mode"), modes, int(doc::BlendMode::Normal)),
            ParamInfo::slider(QStringLiteral("opacity"), nodeTr("Opacity"), 0, 100, 100)};
}

QImage MergeNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &) const
{
    const QImage &back = inputs.value(0);
    QImage front = inputs.value(1);
    if (!front.isNull() && !inputs.value(2).isNull())
        front = applyMask(front, inputs.value(2));
    if (back.isNull())
        return front;
    QImage result = back.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!front.isNull()) {
        const int mode = std::clamp(params.value(QStringLiteral("mode")).toInt(), 0, int(doc::BlendMode::AddGlow));
        canvas::blendImageOver(result, front.size() == result.size() ? front : front.scaled(result.size()), params.value(QStringLiteral("opacity")).toInt() / 100.0,
                               doc::BlendMode(mode));
    }
    return result;
}

} // namespace mothion::compositing
