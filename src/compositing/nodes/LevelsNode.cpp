#include "compositing/nodes/LevelsNode.h"

#include "compositing/ImageOps.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace mothion::compositing {

QImage LevelsNode::process(const QList<QImage> &inputs, const QVariantMap &params, const EvalContext &) const
{
    if (inputs.value(0).isNull())
        return {};
    const int inBlack = params.value(QStringLiteral("inBlack")).toInt();
    const int inWhite = std::max(inBlack + 1, params.value(QStringLiteral("inWhite")).toInt());
    const double gamma = std::max(0.1, params.value(QStringLiteral("gamma")).toInt() / 100.0);
    const int outBlack = params.value(QStringLiteral("outBlack")).toInt();
    const int outWhite = params.value(QStringLiteral("outWhite")).toInt();
    if (inBlack == 0 && inWhite == 255 && gamma == 1.0 && outBlack == 0 && outWhite == 255)
        return inputs.value(0).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    std::array<std::uint8_t, 256> table{};
    for (int v = 0; v < 256; ++v) {
        const double x = std::clamp((v - inBlack) / double(inWhite - inBlack), 0.0, 1.0);
        const double y = outBlack + std::pow(x, 1.0 / gamma) * (outWhite - outBlack);
        table[std::size_t(v)] = std::uint8_t(std::lround(std::clamp(y, 0.0, 255.0)));
    }
    return mapChannels(inputs.value(0), table);
}

} // namespace mothion::compositing
