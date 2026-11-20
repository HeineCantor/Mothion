#include "core/document/LipSync.h"

#include <algorithm>
#include <cctype>

namespace mothion::doc {

std::optional<char> mouthShapeOf(const std::string &drawingName)
{
    const auto first = std::find_if(drawingName.begin(), drawingName.end(),
                                    [](unsigned char c) { return !std::isspace(c); });
    if (first == drawingName.end())
        return std::nullopt;
    const char letter = char(std::toupper(static_cast<unsigned char>(*first)));
    if (std::find(kMouthShapes.begin(), kMouthShapes.end(), letter) == kMouthShapes.end())
        return std::nullopt;
    const auto next = first + 1;
    if (next != drawingName.end() && std::isalnum(static_cast<unsigned char>(*next)))
        return std::nullopt; // a word ("Alpha"), not a shape
    return letter;
}

DrawingId mouthDrawing(const Cel &cel, char shape)
{
    for (const auto &[id, drawing] : cel.drawings()) // by id: the first made
        if (mouthShapeOf(drawing.name) == shape)
            return id;
    return kNoId;
}

} // namespace mothion::doc
