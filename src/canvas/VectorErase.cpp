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
    return t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0 ? t : -1.0;
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

VectorErasure eraseFromStrokes(const std::vector<doc::VectorStroke> &strokes, const std::vector<QPointF> &path,
                               double radius, VectorEraseMode mode)
{
    VectorErasure result;
    if (path.empty()) {
        result.strokes = strokes;
        return result;
    }
    const Path eraser(path);
    std::vector<QRectF> boxes(strokes.size());
    for (std::size_t i = 0; i < strokes.size(); ++i)
        boxes[i] = boundsOf(strokes[i].points).adjusted(-1e-6, -1e-6, 1e-6, 1e-6); // never empty: it must intersect

    for (std::size_t index = 0; index < strokes.size(); ++index) {
        const doc::VectorStroke &stroke = strokes[index];
        const double reach = radius + stroke.size / 2.0;
        const QRectF near = boxes[index].adjusted(-reach, -reach, reach, reach);
        if (stroke.erase || stroke.points.empty() || !near.intersects(eraser.bounds.adjusted(-1e-6, -1e-6, 1e-6, 1e-6))) {
            result.strokes.push_back(stroke);
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
            result.strokes.push_back(stroke);
            continue;
        }
        result.changed = true;
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
                // Out from the touched stretch, to the first crossing with another stroke, or to the stroke's end.
                auto firstCrossing = [&](std::size_t segment, bool forward) -> double {
                    // The crossing nearest the touched stretch on segment (segment, segment + 1): -1 if none.
                    double best = -1.0;
                    const QPointF a = at(q[segment]), b = at(q[segment + 1]);
                    const QRectF segBox = QRectF(a, b).normalized();
                    for (std::size_t o = 0; o < strokes.size(); ++o) {
                        if (o == index || strokes[o].erase || !boxes[o].intersects(segBox.adjusted(-1e-6, -1e-6, 1e-6, 1e-6)))
                            continue;
                        const Points &op = strokes[o].points;
                        for (std::size_t j = 0; j + 1 < op.size(); ++j) {
                            const double t = crossing(a, b, at(op[j]), at(op[j + 1]));
                            if (t < 0.0)
                                continue;
                            // Nearest the stretch: the largest t going back, the smallest going forward.
                            if (best < 0.0 || (forward ? t < best : t > best))
                                best = t;
                        }
                    }
                    return best;
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
            result.strokes.push_back(std::move(piece));
        };
        for (const auto &g : gone) {
            keep(cursor, g.first);
            cursor = g.second;
        }
        keep(cursor, double(q.size() - 1));
    }
    return result;
}

} // namespace mothion::canvas
