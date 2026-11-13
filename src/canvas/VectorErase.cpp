#include "canvas/VectorErase.h"

#include "core/brush/DabGenerator.h"

#include <QLineF>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace mothion::canvas {

namespace {

using brush::BrushInput;
using Points = std::vector<BrushInput>;

QPointF at(const BrushInput &p)
{
    return {p.x, p.y};
}

// The box around points (QRectF::united ignores empty rectangles, so it is done by hand).
struct Extent
{
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    void add(const QPointF &p)
    {
        minX = std::min(minX, p.x());
        minY = std::min(minY, p.y());
        maxX = std::max(maxX, p.x());
        maxY = std::max(maxY, p.y());
    }
    QRectF rect() const { return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)); }
};

double pointSegmentDistance(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = 0.0;
    if (length2 > 1e-12)
        t = std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / length2, 0.0, 1.0);
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

// Where segment ab crosses cd: the position along ab (0..1), or a negative number if it does not.
double crossing(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d)
{
    const QPointF r = b - a, s = d - c;
    const double denom = r.x() * s.y() - r.y() * s.x();
    if (std::abs(denom) < 1e-12)
        return -1.0; // parallel
    const QPointF ca = c - a;
    const double t = (ca.x() * s.y() - ca.y() * s.x()) / denom;
    const double u = (ca.x() * r.y() - ca.y() * r.x()) / denom;
    // A line that ends on the crossing (a stroke cut there a moment ago) still crosses it: the end is hit up to rounding.
    constexpr double kEnd = 1e-6;
    return t >= -kEnd && t <= 1.0 + kEnd && u >= -kEnd && u <= 1.0 + kEnd ? std::clamp(t, 0.0, 1.0) : -1.0;
}

double segmentDistance(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d)
{
    if (crossing(a, b, c, d) >= 0.0)
        return 0.0;
    return std::min({pointSegmentDistance(a, c, d), pointSegmentDistance(b, c, d), pointSegmentDistance(c, a, b),
                     pointSegmentDistance(d, a, b)});
}

// The path as segments (a single point is a segment of no length).
struct Path
{
    std::vector<std::pair<QPointF, QPointF>> segments;
    QRectF bounds;

    explicit Path(const std::vector<QPointF> &points)
    {
        for (std::size_t i = 0; i < points.size(); ++i)
            segments.emplace_back(points[i], points[i + 1 < points.size() ? i + 1 : i]);
        Extent extent;
        for (const QPointF &p : points)
            extent.add(p);
        bounds = extent.rect();
    }

    // How near the path comes to the segment ab.
    double distanceTo(const QPointF &a, const QPointF &b) const
    {
        double best = 1e300;
        for (const auto &seg : segments)
            best = std::min(best, segmentDistance(a, b, seg.first, seg.second));
        return best;
    }

    // Whether the path comes within `reach` of segment ab.
    bool reaches(const QPointF &a, const QPointF &b, double reach) const
    {
        const QRectF box = QRectF(a, b).normalized().adjusted(-reach, -reach, reach, reach);
        for (const auto &s : segments) {
            if (!box.intersects(QRectF(s.first, s.second).normalized().adjusted(-1e-6, -1e-6, 1e-6, 1e-6)))
                continue;
            if (segmentDistance(a, b, s.first, s.second) <= reach)
                return true;
        }
        return false;
    }
    bool reaches(const QPointF &p, double reach) const { return reaches(p, p, reach); }
};

QRectF boundsOf(const Points &points)
{
    Extent extent;
    for (const BrushInput &p : points)
        extent.add(at(p));
    return extent.rect();
}

// The box around a stroke's line and its width.
QRectF strokeBox(const doc::VectorStroke &stroke)
{
    const double margin = stroke.size * 1.5 + 2.0; // a soft edge reaches past the radius
    return boundsOf(stroke.points).adjusted(-margin, -margin, margin, margin);
}

// The points from continuous position `a` to `b` along `q` (an integer is a point of `q`), the ends put on the line.
Points slice(const Points &q, double a, double b)
{
    auto pointAt = [&](double s) {
        const std::size_t i = std::min(std::size_t(std::floor(s)), q.size() - 1);
        const double t = s - double(i);
        if (t < 1e-9 || i + 1 >= q.size())
            return q[i];
        return brush::DabGenerator::interpolate(q[i], q[i + 1], t);
    };
    Points out{pointAt(a)};
    for (std::size_t k = std::size_t(std::floor(a)) + 1; double(k) < b && k < q.size(); ++k)
        if (double(k) > a)
            out.push_back(q[k]);
    out.push_back(pointAt(b));
    // No point twice in a row.
    Points clean;
    for (const BrushInput &p : out)
        if (clean.empty() || QLineF(at(clean.back()), at(p)).length() > 1e-6)
            clean.push_back(p);
    return clean;
}

} // namespace

VectorEraseState::VectorEraseState(std::vector<doc::VectorStroke> original)
    : m_original(std::move(original))
{
    for (std::size_t i = 0; i < m_original.size(); ++i) {
        m_originalBoxes.push_back(boundsOf(m_original[i].points).adjusted(-1e-6, -1e-6, 1e-6, 1e-6));
        m_left.push_back({m_original[i], i, strokeBox(m_original[i])});
    }
}

std::vector<doc::VectorStroke> VectorEraseState::strokes() const
{
    std::vector<doc::VectorStroke> out;
    out.reserve(m_left.size());
    for (const Entry &e : m_left)
        out.push_back(e.stroke);
    return out;
}

QRectF VectorEraseState::takeDirty()
{
    const QRectF dirty = m_hasDirty ? m_dirty : QRectF();
    m_hasDirty = false;
    return dirty;
}

std::vector<doc::VectorStroke> VectorEraseState::strokesIn(const QRectF &area) const
{
    std::vector<doc::VectorStroke> out;
    for (const Entry &e : m_left)
        if (e.box.intersects(area))
            out.push_back(e.stroke);
    return out;
}

void VectorEraseState::erase(const std::vector<QPointF> &pathPoints, double radius, VectorEraseMode mode, bool pressed)
{
    if (pathPoints.empty())
        return;
    const Path eraser(pathPoints);
    std::vector<Entry> next;
    next.reserve(m_left.size());

    // Up to the intersection, the eraser's circle does not say what is taken: only the eraser's own path does. A circle
    // that reaches over a crossing touches both lines there, or the start of the piece beyond a cut made a moment ago,
    // and what it touched would be taken too (the erasing running on past the crossing, depending on what was
    // erased before and how far the pen went on). So a stroke is taken only where the path goes over its ink (within
    // half its width on the press, within a hair of its line after), and of those only the ones the path passes closest to (all it crosses; at a crossing of two
    // thick lines, the one it is on), and only the stretch of it within a hair of the path.
    constexpr double kCoreTolerance = 2.0; // px
    std::vector<double> nearest(m_left.size(), 1e300);
    double best = 1e300;
    if (mode == VectorEraseMode::ToIntersection) {
        for (std::size_t e = 0; e < m_left.size(); ++e) {
            const doc::VectorStroke &stroke = m_left[e].stroke;
            const double body = (pressed ? stroke.size / 2.0 : 0.0) + kCoreTolerance;
            if (stroke.erase || stroke.points.empty()
                || !m_left[e].box.adjusted(-body, -body, body, body)
                        .intersects(eraser.bounds.adjusted(-1e-6, -1e-6, 1e-6, 1e-6)))
                continue;
            double d = eraser.distanceTo(at(stroke.points.front()), at(stroke.points.front()));
            for (std::size_t i = 1; i < stroke.points.size() && d > 0.0; ++i)
                d = std::min(d, eraser.distanceTo(at(stroke.points[i - 1]), at(stroke.points[i])));
            if (d <= body) {
                nearest[e] = d;
                best = std::min(best, d);
            }
        }
    }

    std::size_t entryIndex = 0;
    for (Entry &entry : m_left) {
        const std::size_t e = entryIndex++;
        const doc::VectorStroke &stroke = entry.stroke;
        double reach = radius + stroke.size / 2.0;
        if (mode == VectorEraseMode::ToIntersection) {
            if (nearest[e] > 1e299 || nearest[e] > best + kCoreTolerance) { // not one the eraser went by
                next.push_back(std::move(entry));
                continue;
            }
            reach = nearest[e] + kCoreTolerance;
        }
        const QRectF near = entry.box.adjusted(-reach, -reach, reach, reach);
        if (stroke.erase || stroke.points.empty() || !near.intersects(eraser.bounds.adjusted(-1e-6, -1e-6, 1e-6, 1e-6))) {
            next.push_back(std::move(entry));
            continue;
        }

        // The line, with more points where the eraser comes by, so that it is cut close to the circle's edge.
        const double step = std::clamp(reach / 4.0, 0.5, 2.0);
        Points q;
        q.push_back(stroke.points.front());
        for (std::size_t i = 1; i < stroke.points.size(); ++i) {
            const BrushInput &a = stroke.points[i - 1], &b = stroke.points[i];
            if (eraser.reaches(at(a), at(b), reach)) {
                const int parts = std::max(1, int(std::ceil(QLineF(at(a), at(b)).length() / step)));
                for (int k = 1; k < parts; ++k)
                    q.push_back(brush::DabGenerator::interpolate(a, b, double(k) / parts));
            }
            q.push_back(b);
        }
        std::vector<char> touched(q.size());
        bool any = false;
        for (std::size_t i = 0; i < q.size(); ++i) {
            touched[i] = eraser.reaches(at(q[i]), reach);
            any |= touched[i];
        }
        if (!any) {
            next.push_back(std::move(entry));
            continue;
        }
        m_changed = true;
        m_dirty = m_hasDirty ? m_dirty.united(entry.box) : entry.box;
        m_hasDirty = true;
        if (mode == VectorEraseMode::WholeLine)
            continue;

        // What goes, as stretches of positions along `q`.
        std::vector<std::pair<double, double>> gone;
        for (std::size_t i = 0; i < q.size();) {
            if (!touched[i]) {
                ++i;
                continue;
            }
            std::size_t last = i;
            while (last + 1 < q.size() && touched[last + 1])
                ++last;
            double from = double(i), to = double(last);
            if (mode == VectorEraseMode::ToIntersection) {
                // Out from the touched stretch, to the first crossing with another stroke (as it was drawn), or to the
                // stroke's end.
                auto firstCrossing = [&](std::size_t segment, bool forward) -> double {
                    // The crossing nearest the touched stretch on segment (segment, segment + 1): -1 if none.
                    double nearestT = -1.0;
                    const QPointF a = at(q[segment]), b = at(q[segment + 1]);
                    const QRectF segBox = QRectF(a, b).normalized().adjusted(-1e-6, -1e-6, 1e-6, 1e-6);
                    for (std::size_t o = 0; o < m_original.size(); ++o) {
                        if (o == entry.origin || m_original[o].erase || !m_originalBoxes[o].intersects(segBox))
                            continue;
                        const Points &op = m_original[o].points;
                        for (std::size_t j = 0; j + 1 < op.size(); ++j) {
                            const double t = crossing(a, b, at(op[j]), at(op[j + 1]));
                            if (t < 0.0)
                                continue;
                            // Nearest the stretch: the largest t going back, the smallest going forward.
                            if (nearestT < 0.0 || (forward ? t < nearestT : t > nearestT))
                                nearestT = t;
                        }
                    }
                    return nearestT;
                };
                from = 0.0;
                for (std::size_t j = i; j > 0; --j) {
                    const double t = firstCrossing(j - 1, false);
                    if (t >= 0.0) {
                        from = double(j - 1) + t;
                        break;
                    }
                }
                to = double(q.size() - 1);
                for (std::size_t j = last; j + 1 < q.size(); ++j) {
                    const double t = firstCrossing(j, true);
                    if (t >= 0.0) {
                        to = double(j) + t;
                        break;
                    }
                }
            }
            if (!gone.empty() && from <= gone.back().second)
                gone.back().second = std::max(gone.back().second, to);
            else
                gone.emplace_back(from, to);
            // The next stretch starts after this one (and past what went with it).
            i = std::max(last + 1, std::size_t(std::floor(to)) + 1);
        }

        // What stays: the pieces between the stretches that go.
        double cursor = 0.0;
        auto keep = [&](double a, double b) {
            if (b - a <= 1e-9)
                return;
            Points points = slice(q, a, b);
            if (points.size() < 2)
                return;
            doc::VectorStroke piece = stroke;
            piece.points = std::move(points);
            const QRectF box = strokeBox(piece);
            next.push_back({std::move(piece), entry.origin, box});
        };
        for (const auto &g : gone) {
            keep(cursor, g.first);
            cursor = g.second;
        }
        keep(cursor, double(q.size() - 1));
    }
    m_left = std::move(next);
}

VectorErasure eraseFromStrokes(const std::vector<doc::VectorStroke> &strokes, const std::vector<QPointF> &path,
                               double radius, VectorEraseMode mode)
{
    VectorEraseState state(strokes);
    state.erase(path, radius, mode);
    VectorErasure result;
    result.strokes = state.strokes();
    result.changed = state.changed();
    return result;
}

} // namespace mothion::canvas
