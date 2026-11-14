#pragma once

#include <QPainterPath>

namespace mothion::canvas {

// `area` with its edge moved out (`pixels` > 0) or in (< 0) by that distance, corners rounded where it goes around
// them: what lies within `pixels` of the area is added, or what lies within it of the outside is taken away. Worked
// on the pixel grid (a distance map of the rasterized area, outlined again), so it takes the same time however
// detailed the outline is. Empty when nothing is left.
QPainterPath grownSelection(const QPainterPath &area, double pixels);

} // namespace mothion::canvas
