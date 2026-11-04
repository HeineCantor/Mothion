#pragma once

#include "core/perspective/PerspectiveGrid.h"

#include <QChar>
#include <QColor>
#include <QPointF>

#include <array>
#include <optional>

namespace mothion::canvas {

// Keeps a stroke on one of the three axes of a perspective grid.
//
// An axis, seen from the sheet, is a pencil of lines through its vanishing point (or parallel
// lines when the axis lies in the picture's plane). The stroke starts at a point; the line of each
// axis through that point is a candidate. Once the pen has gone far enough from the start to
// show where it is heading, the candidate closest in direction to the pen is chosen and kept for
// the rest of the stroke; from then on every pen point is put on that line.
//
// Document coordinates throughout.
class PerspectiveSnap
{
public:
    // Starts a stroke at `start`; the axis is decided once the pen is `threshold` px away.
    void begin(const perspective::Grid &grid, const QPointF &start, double threshold);
    void reset();

    // The pen is at `pen`: decides the axis if it can now. True once decided.
    bool update(const QPointF &pen);
    bool decided() const { return m_axis >= 0; }
    // 0, 1, 2: the plane's x, y and normal. -1 while undecided.
    int axis() const { return m_axis; }
    // Takes the next axis (x, y, normal, x...) in place of the chosen one, deciding it if it was not
    // yet: the line is then on that axis, from the same start. Returns it (-1 if none has a line).
    int cycle();
    // Where the line starts.
    QPointF anchor() const { return m_start; }
    // The color and letter that tell the axes apart (the colors of the perspective layer's handles).
    static QColor axisColor(int axis);
    static QChar axisLetter(int axis);
    // `pen` put on the chosen line (as is while undecided).
    QPointF snapped(const QPointF &pen) const;

    // The direction (unit) of each axis's line through `from`, nothing for one that has none
    // there (when `from` is at the vanishing point itself).
    static std::array<std::optional<QPointF>, 3> directions(const perspective::Grid &grid, const QPointF &from);

private:
    std::array<std::optional<QPointF>, 3> m_directions;
    QPointF m_start;
    double m_threshold = 0.0;
    int m_axis = -1;
};

} // namespace mothion::canvas
