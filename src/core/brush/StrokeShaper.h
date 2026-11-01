#pragma once

#include "core/brush/BrushStyle.h"

#include <vector>

namespace mothion::brush {

// Stroke-level shaping: the anti-jitter filter on incoming points, the
// start/end factor and start taper by distance along the stroke (applied
// dab by dab in DabGenerator), and the corrections that need the finished
// stroke. When one of those applies, the canvas redraws the stroke when the
// pen lifts, with the same seed and the stroke's length known: up to where
// the end zone starts, the redraw places exactly the same dabs.

// Anti-jitter: true if `b`, the last point, is a wobble between `a` and the
// new point `c`, and should be replaced by `c`. `factor` scales the lengths
// that pick the angle threshold (0.05 at 100% zoom).
bool isJitter(const BrushInput &a, const BrushInput &b, const BrushInput &c, double factor = 0.05);

// Start/end factor at `distance` px along a stroke of `total` px; `total`
// < 0 while the end is not known yet (no end zone, no ratio zones).
double inOutFactor(const InOutStyle &style, double distance, double total);

// Pressure factor of the start taper: sin(90°·d/length), 1 past `length`.
double startTaperFactor(double length, double distance);

// True if some parameter follows the start/end factor.
bool usesInOut(const BrushStyle &style);

// True if the finished stroke must be redrawn: its look depends on where it
// ends (end zone, ratio zones) or it is simplified.
bool needsFinalPass(const BrushStyle &style);

// The finished stroke's points as they are drawn for good (simplified when
// the brush asks for it).
std::vector<BrushInput> finalPoints(const BrushStyle &style, std::vector<BrushInput> points);

double strokeLength(const std::vector<BrushInput> &points);

// Drops points while the stroke stays within a tolerance growing with
// `level`, then joins the rest with a smooth (Catmull-Rom) curve.
std::vector<BrushInput> simplify(const std::vector<BrushInput> &points, int level);

} // namespace mothion::brush
