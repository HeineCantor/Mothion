#pragma once

#include <algorithm>
#include <cmath>

namespace mothion::ui {

// Geometry of the timeline view: sizes of the regions and frame <-> x mapping.
//
//   +-----------+---------------------------+
//   |  corner   |  ruler (frame numbers)    |  rulerHeight
//   +-----------+---------------------------+
//   |  cel      |  cells (exposures)        |  rowHeight per track
//   |  names    |                           |
//   +-----------+---------------------------+
//    headerWidth   frameWidth per frame, scrolled by scrollX
struct TimelineMetrics
{
    int headerWidth = 130;
    int rulerHeight = 24;
    int rowHeight = 22;
    int frameWidth = 16;
    int scrollX = 0; // horizontal scroll of ruler + cells, in pixels
    int scrollY = 0; // vertical scroll of names + cells, in pixels

    // Left edge of a frame in viewport coordinates.
    int frameX(int frame) const { return headerWidth + frame * frameWidth - scrollX; }
    int rowY(int row) const { return rulerHeight + row * rowHeight - scrollY; }

    // Frame under a viewport x (may be out of range; callers clamp).
    int frameAt(int x) const
    {
        return int(std::floor(double(x - headerWidth + scrollX) / frameWidth));
    }
};

} // namespace mothion::ui
