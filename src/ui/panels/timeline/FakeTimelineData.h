#pragma once

#include <QColor>
#include <QString>

#include <vector>

namespace mothion::ui {

// Placeholder data for the timeline panel, generated in memory.
// NOT the future document model: just enough structure to draw an
// exposure-based timeline (one row per layer, drawings held over frames).

struct Exposure
{
    int start = 0;   // first frame, 0-based
    int length = 1;  // number of frames the drawing is held
    QString drawing; // drawing name shown in the cell (Harmony style)
};

struct TimelineTrack
{
    QString name;
    QColor color;
    std::vector<Exposure> exposures; // sorted, non-overlapping; gaps = empty frames
};

struct FakeTimeline
{
    int frameCount = 0;
    int fps = 24;
    std::vector<TimelineTrack> tracks;
};

FakeTimeline makeFakeTimeline();

} // namespace mothion::ui
