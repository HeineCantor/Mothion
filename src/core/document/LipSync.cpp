#include "core/document/LipSync.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

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

std::vector<TimedPhone> ctcPhones(const float *logProbs, int frames, const std::vector<std::string> &tokens,
                                  double frameSeconds, double firstFrameSeconds)
{
    constexpr double kOutlast = 0.12; // how long a phone lasts past its run of frames when a pause follows
    const int vocab = int(tokens.size());
    std::vector<TimedPhone> phones;
    if (!logProbs || frames <= 0 || vocab == 0)
        return phones;
    auto isPhone = [&](int id) {
        const std::string &t = tokens[std::size_t(id)];
        return id != 0 && !t.empty() && t.front() != '<' && t != "▁";
    };
    struct Run
    {
        int token, first, last;
    };
    std::vector<Run> runs;
    int previous = -1;
    for (int t = 0; t < frames; ++t) {
        const float *row = logProbs + std::size_t(t) * std::size_t(vocab);
        const int best = int(std::max_element(row, row + vocab) - row);
        if (best == previous && !runs.empty() && runs.back().token == best && runs.back().last == t - 1)
            runs.back().last = t;
        else if (best != previous && isPhone(best))
            runs.push_back({best, t, t});
        previous = best;
    }
    auto time = [&](int frame) { return firstFrameSeconds + frame * frameSeconds; };
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const double start = time(runs[i].first);
        const double next = i + 1 < runs.size() ? time(runs[i + 1].first) : time(frames);
        phones.push_back({tokens[std::size_t(runs[i].token)], start, std::min(next, time(runs[i].last + 1) + kOutlast)});
    }
    return phones;
}

namespace {

// The code points of a UTF-8 string (a malformed byte is taken as it is).
std::vector<char32_t> codePoints(const std::string &s)
{
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
        if (extra < 0 || i + std::size_t(extra) >= s.size()) {
            out.push_back(c);
            ++i;
            continue;
        }
        char32_t cp = extra == 0 ? c : c & (0x3F >> extra);
        for (int k = 1; k <= extra; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + std::size_t(k)]) & 0x3F);
        out.push_back(cp);
        i += std::size_t(extra) + 1;
    }
    return out;
}

bool isMark(char32_t c)
{
    return (c >= 0x0300 && c <= 0x036F)    // combining diacritics (nasal, voiceless...)
           || (c >= 0x02B0 && c <= 0x02FF) // modifier letters (ʰ ʲ ʷ ː ˞...)
           || (c >= 0x1DC0 && c <= 0x1DFF) || c == 0x2019 || c == '\'';
}

} // namespace

std::optional<char> mouthShapeOfPhone(const std::string &phone)
{
    static const std::pair<std::u32string_view, char> kShapes[] = {
        {U"pbmɓʙɸβ", 'A'},       // lips closed
        {U"fvɱⱱʋ", 'G'},         // teeth on the lip
        {U"lɫɭʎɬɮʟɺ", 'H'},      // tongue up
        {U"uʊwʍyʏøɵɥʉ", 'F'},    // puckered
        {U"oɔɒœɜɝɚɞ", 'E'},      // rounded
        {U"aɑɐɶ", 'D'},          // wide open
        {U"eɛəʌɘɤæ", 'C'},       // open
    };
    for (const char32_t c : codePoints(phone)) {
        if (isMark(c))
            continue;
        for (const auto &[letters, shape] : kShapes)
            if (letters.find(c) != std::u32string_view::npos)
                return shape;
        return 'B'; // the other consonants, I and the like: teeth together
    }
    return std::nullopt;
}

std::vector<MouthCue> mouthCuesFromPhones(const std::vector<TimedPhone> &phones, double duration,
                                          const std::string &extendedShapes)
{
    constexpr double kBridge = 0.12; // a pause shorter than this keeps the mouth it falls after
    auto has = [&](char shape) { return extendedShapes.find(shape) != std::string::npos; };
    auto available = [&](char shape) {
        if (shape == 'G' && !has('G'))
            return 'B';
        if (shape == 'H' && !has('H'))
            return 'C';
        if (shape == 'X' && !has('X'))
            return 'A';
        return shape;
    };
    std::vector<MouthCue> cues;
    auto put = [&](double start, double end, char shape) {
        if (end - start <= 1e-9)
            return;
        if (!cues.empty() && cues.back().shape == shape && std::abs(cues.back().end - start) < 1e-9)
            cues.back().end = end;
        else
            cues.push_back({start, end, shape});
    };
    double t = 0.0;
    for (const TimedPhone &p : phones) {
        const auto shape = mouthShapeOfPhone(p.phone);
        if (!shape)
            continue;
        if (p.start > t + 1e-9) {
            // A pause: a rest, unless too short to close the mouth (the mouth before holds through it).
            if (!cues.empty() && p.start - t < kBridge)
                cues.back().end = p.start;
            else
                put(t, p.start, 'X');
        }
        put(std::max(p.start, t), p.end, *shape);
        t = std::max(t, p.end);
    }
    put(t, std::max(t, duration), 'X');
    for (MouthCue &cue : cues)
        cue.shape = available(cue.shape);
    // Neighbours that became the same shape are one cue.
    std::vector<MouthCue> merged;
    for (const MouthCue &cue : cues)
        if (!merged.empty() && merged.back().shape == cue.shape && std::abs(merged.back().end - cue.start) < 1e-9)
            merged.back().end = cue.end;
        else
            merged.push_back(cue);
    return merged;
}

} // namespace mothion::doc
