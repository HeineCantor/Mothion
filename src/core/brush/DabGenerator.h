#pragma once

#include "core/brush/BrushStyle.h"
#include "core/brush/Random.h"

#include <cstdint>
#include <vector>

namespace mothion::brush {

// Turns a stroke into dabs: where they go and what each one looks like.
//
// Feed it the stroke's points (document position plus pen state) with
// begin() / continueTo(). Between two points the pen state is interpolated
// along the segment and a dab is placed, then the dab itself says how far
// the next one is: spacing is recomputed at every dab from its size,
// hardness and dynamics, so random spacing gives uneven gaps. The leftover
// distance carries over to the next segment.
//
// Everything random comes from one Lcg seeded per stroke: same seed and
// same points give the same dabs. The generator knows nothing about pixels.
class DabGenerator
{
public:
    explicit DabGenerator(const BrushStyle &style, std::uint32_t seed = 1);

    // Starts a stroke: the dabs at the first point.
    std::vector<BrushDab> begin(const BrushInput &first);
    // Dabs from the previous point up to `next` (may be empty for tiny moves).
    std::vector<BrushDab> continueTo(const BrushInput &next);

    // The whole stroke's length, when it is known (redrawing a finished
    // stroke): enables the end zone of the start/end factor. Until then
    // only the start is shaped.
    void setStrokeLength(double length) { m_strokeLength = length; }

    const BrushStyle &style() const { return m_style; }

    // One dab at `in` and the distance to the next one, without spray.
    // `applyRandom` false leaves out the random factors (spray particles
    // draw their own). Exposed for tests and previews.
    struct Single
    {
        BrushDab dab;
        double spacing = 1.0;
        bool skip = false; // e.g. TipOrder::Once ran out of tips
    };
    Single computeDab(const BrushInput &in, bool applyRandom);

    // Size and alpha of a dab at `in`, taken as the point the stroke has
    // reached (start/end factor and start taper there), without randomness
    // and without touching the generator's state: for what is drawn beside
    // the dabs, such as the tip of a sharp corner.
    BrushDab dabHere(const BrushInput &in) const;

    // Interpolates two pen states (positions linearly, angles the short way round).
    static BrushInput interpolate(const BrushInput &a, const BrushInput &b, double t);

private:
    // All dabs for one position along the stroke (one, or the spray particles).
    double place(const BrushInput &in, std::vector<BrushDab> &out);
    // The start/end factor and start taper at `distance` px along the stroke.
    BrushInput shaped(BrushInput in, double distance) const;
    int chooseTip(bool &mirror, double &extraAngle);
    double spacingFraction(const BrushInput &in, double thickness, bool applyRandom);

    BrushStyle m_style;
    Lcg m_rng;
    BrushInput m_last;
    double m_remainder = 0.0; // distance from the last point to the next dab
    double m_travelled = 0.0; // stroke length up to the last point
    double m_strokeLength = -1.0; // whole stroke, < 0 = not known
    int m_dabIndex = 0;       // counts dab positions for the tip order
    DabColor m_strokeColor;   // per-stroke color shift, drawn in begin()
};

} // namespace mothion::brush
