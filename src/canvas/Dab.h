#pragma once

#include <QPointF>

namespace mothion::canvas {

// A round brush stamp in document coordinates.
struct Dab
{
    QPointF center;
    double radius = 1.0;
};

} // namespace mothion::canvas
