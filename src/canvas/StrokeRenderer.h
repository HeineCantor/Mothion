#pragma once

#include "canvas/RasterSurface.h"

#include <QRect>

namespace mothion::canvas {

// Turns stroke points (document position + radius) into evenly spaced dabs.
//
// Pen samples arrive at uneven distances; stamping only at the samples would
// leave gaps on fast strokes. Between two points we interpolate position and
// radius and place a dab every `spacing` pixels, carrying the remainder over
// to the next segment so spacing stays even across samples.
class StrokeRenderer
{
public:
    struct Point
    {
        QPointF pos;   // document coordinates
        double radius; // document pixels
    };

    // Starts a stroke with a single dab. Returns the changed document rect.
    QRect begin(const Point &p, RasterSurface &surface);
    // Extends the stroke to p. Returns the changed document rect.
    QRect continueTo(const Point &p, RasterSurface &surface);

private:
    Point m_last{};
    double m_distanceSinceDab = 0.0;
};

} // namespace mothion::canvas
