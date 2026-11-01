#pragma once

#include "core/brush/BrushStyle.h"

#include <vector>

namespace mothion::brush {

// What happens to a stroke's points (position plus pen state) before they
// become dabs: the anti-jitter filter while drawing, and, when the pen
// lifts, the corrections that need the whole stroke (start/end of stroke,
// start pressure ramp, simplification). The canvas then redraws the stroke
// from finalPoints() with the same random seed.

// Anti-jitter: true if `b`, the last point, is a wobble between `a` and the
// new point `c`, and should be replaced by `c`. `factor` scales the lengths
// that pick the angle threshold (0.05 at 100% zoom).
bool isJitter(const BrushInput &a, const BrushInput &b, const BrushInput &c, double factor = 0.05);

// Start/end factor at `distance` px from the start, while the end is not
// known yet: the start ramp (Length) or the fade (Fade); 1 otherwise.
double liveInOut(const InOutStyle &style, double distance);

// True if some parameter follows the start/end factor.
bool usesInOut(const BrushStyle &style);

// True if the finished stroke must be redrawn from finalPoints().
bool needsFinalPass(const BrushStyle &style);

// The finished stroke's points as they are drawn for good.
std::vector<BrushInput> finalPoints(const BrushStyle &style, std::vector<BrushInput> points);

// Building blocks of finalPoints(), exposed for tests.

// Pressure × sin(90°·d/len) over the first `count` points, len = their length (≥ 2 px).
void applyStartRamp(std::vector<BrushInput> &points, int count);
// Splits the start and end zones every quarter of their length and gives
// each point its factor: sin(90°·d/len), d = distance from that end.
std::vector<BrushInput> applyInOut(const std::vector<BrushInput> &points, const InOutStyle &style);
// Drops points while the stroke stays within a tolerance growing with
// `level`, then joins the rest with a smooth (Catmull-Rom) curve.
std::vector<BrushInput> simplify(const std::vector<BrushInput> &points, int level);

} // namespace mothion::brush
