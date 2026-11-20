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

Timing lipSyncTiming(const Cel &cel, const std::vector<MouthCue> &cues, double fps, int offset, int minFrames,
                     int frameCount)
{
    Timing timing = cel.timing();
    if (cues.empty() || fps <= 0)
        return timing;
    minFrames = std::max(1, minFrames);
    auto frameOf = [&](double seconds) { return offset + int(std::lround(seconds * fps)); };
    // Where each mouth begins (the shapes kept), then each held to where the next begins.
    std::vector<std::pair<int, DrawingId>> starts;
    for (const MouthCue &cue : cues) {
        const DrawingId drawing = mouthDrawing(cel, cue.shape);
        if (drawing == kNoId)
            continue;
        const int frame = frameOf(cue.start);
        if (!starts.empty() && (starts.back().second == drawing || frame < starts.back().first + minFrames))
            continue;
        starts.emplace_back(frame, drawing);
    }
    const int end = frameOf(cues.back().end);
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const int from = std::max(0, starts[i].first);
        const int to = std::min(frameCount, i + 1 < starts.size() ? starts[i + 1].first : std::max(end, from + 1));
        if (to > from)
            timing.setExposure(from, to - from, starts[i].second);
    }
    return timing;
}

} // namespace mothion::doc
