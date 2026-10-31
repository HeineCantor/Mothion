#include "ui/panels/timeline/FakeTimelineData.h"

#include <initializer_list>

namespace mothion::ui {

namespace {

// Lays out exposures back to back with the given hold lengths, naming the
// drawings with `names` in order (cycling), until `frameCount` is filled.
std::vector<Exposure> sequence(int frameCount, std::initializer_list<int> holds,
                               std::initializer_list<const char *> names, int firstFrame = 0)
{
    std::vector<Exposure> out;
    const std::vector<int> h(holds);
    const std::vector<const char *> n(names);
    int frame = firstFrame;
    for (std::size_t i = 0; frame < frameCount; ++i) {
        const int length = std::min(h[i % h.size()], frameCount - frame);
        out.push_back({frame, length, QString::fromLatin1(n[i % n.size()])});
        frame += length;
    }
    return out;
}

} // namespace

FakeTimeline makeFakeTimeline()
{
    FakeTimeline t;
    t.frameCount = 96; // 4 seconds at 24 fps
    t.fps = 24;

    t.tracks.push_back({QStringLiteral("Effetti"), QColor(0xb0, 0x6a, 0xd0),
                        {{30, 4, QStringLiteral("fx1")}, {34, 4, QStringLiteral("fx2")},
                         {38, 6, QStringLiteral("fx3")}, {70, 12, QStringLiteral("fx4")}}});
    t.tracks.push_back({QStringLiteral("Bocca"), QColor(0xd0, 0x8a, 0x4a),
                        sequence(t.frameCount, {3, 2, 4, 2, 6, 3, 2, 5},
                                 {"X", "A", "E", "O", "X", "U", "A", "M"}, 6)});
    t.tracks.push_back({QStringLiteral("Personaggio"), QColor(0x4f, 0x8c, 0xd6),
                        sequence(t.frameCount, {2, 2, 2, 6, 2, 2, 3, 3, 8},
                                 {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"})});
    t.tracks.push_back({QStringLiteral("Ombra"), QColor(0x5a, 0x9a, 0x6a),
                        sequence(48, {4}, {"s1", "s2", "s3"}, 12)});
    t.tracks.push_back({QStringLiteral("Sfondo"), QColor(0x80, 0x80, 0x88),
                        {{0, t.frameCount, QStringLiteral("bg_A")}}});
    return t;
}

} // namespace mothion::ui
