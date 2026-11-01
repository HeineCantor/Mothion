#pragma once

#include <algorithm>

namespace mothion::brush {

// Coverage of a round dab at normalized distance r from the centre (1 = the
// dab's radius, which already includes the soft-edge extension).
//
// Solid out to h' = 1.3·hardness − 0.3, then an S-shaped quadratic falloff
// to 0 at r = 1. Hardness 1 is a plain hard disc.
inline double hardnessCoverage(double r, double hardness)
{
    if (r >= 1.0)
        return 0.0;
    if (hardness >= 0.99999999)
        return 1.0;
    const double inner = std::clamp(hardness, 0.0, 1.0) * 1.3 - 0.3;
    if (r < inner)
        return 1.0;
    const double s = (r - inner) / (1.0 - inner);
    return r >= 1.0 - (1.0 - inner) * 0.5 ? 2.0 * (1.0 - s) * (1.0 - s) : 1.0 - 2.0 * s * s;
}

} // namespace mothion::brush
