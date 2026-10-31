#pragma once

#include "canvas/Dab.h"

#include <vector>

namespace mothion::canvas {

// Turns stroke points (document position + radius) into evenly spaced dabs.
//
// Pen samples arrive at uneven distances; stamping only at the samples would
// leave gaps on fast strokes. Between two points we interpolate position and
// radius and place a dab every `spacing` pixels, carrying the remainder over
// to the next segment so spacing stays even across samples.
// It only computes where dabs go; StrokeSession puts them on pixels.
class DabSpacer
{
public:
    struct Point
    {
        QPointF pos;   // document coordinates
        double radius; // document pixels
    };

    // Starts a stroke: a single dab at p.
    std::vector<Dab> begin(const Point &p);
    // Dabs from the previous point to p (may be empty for tiny moves).
    std::vector<Dab> continueTo(const Point &p);

private:
    Point m_last{};
    double m_distanceSinceDab = 0.0;
};

} // namespace mothion::canvas
