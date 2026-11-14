#include "canvas/SelectionGrow.h"

#include "canvas/SelectionMask.h"
#include "core/puppet/Contour.h"

#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace mothion::canvas {

namespace {

constexpr float kFar = 1e20f;

// The squared distance of each sample to the nearest one where `f` is 0 along a line (Felzenszwalb and
// Huttenlocher's lower envelope of parabolas), in place; `n` samples `stride` apart.
void distanceAlong(float *f, int n, int stride, std::vector<float> &d, std::vector<int> &v, std::vector<float> &z)
{
    d.resize(std::size_t(n));
    v.resize(std::size_t(n));
    z.resize(std::size_t(n) + 1);
    int k = 0;
    v[0] = 0;
    z[0] = -std::numeric_limits<float>::infinity();
    z[1] = std::numeric_limits<float>::infinity();
    for (int q = 1; q < n; ++q) {
        float s;
        while (true) {
            const int p = v[std::size_t(k)];
            s = ((f[q * stride] + float(q) * float(q)) - (f[p * stride] + float(p) * float(p))) / float(2 * q - 2 * p);
            if (s > z[std::size_t(k)] || k == 0)
                break;
            --k;
        }
        if (s <= z[std::size_t(k)]) { // k == 0 and the first parabola is entirely above
            v[0] = q;
            z[1] = std::numeric_limits<float>::infinity();
            continue;
        }
        ++k;
        v[std::size_t(k)] = q;
        z[std::size_t(k)] = s;
        z[std::size_t(k) + 1] = std::numeric_limits<float>::infinity();
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[std::size_t(k) + 1] < float(q))
            ++k;
        const int p = v[std::size_t(k)];
        d[std::size_t(q)] = float(q - p) * float(q - p) + f[p * stride];
    }
    for (int q = 0; q < n; ++q)
        f[q * stride] = d[std::size_t(q)];
}

// The Euclidean distance of each pixel to the nearest pixel where `target` is true.
std::vector<float> distanceTo(const std::vector<std::uint8_t> &target, int w, int h)
{
    std::vector<float> f(target.size());
    for (std::size_t i = 0; i < target.size(); ++i)
        f[i] = target[i] ? 0.0f : kFar;
    std::vector<float> d, z;
    std::vector<int> v;
    for (int x = 0; x < w; ++x)
        distanceAlong(f.data() + x, h, w, d, v, z);
    for (int y = 0; y < h; ++y)
        distanceAlong(f.data() + std::size_t(y) * std::size_t(w), w, 1, d, v, z);
    for (float &value : f)
        value = std::sqrt(value);
    return f;
}

} // namespace

QPainterPath grownSelection(const QPainterPath &area, double pixels)
{
    const QRect bounds = area.boundingRect().toAlignedRect();
    if (bounds.isEmpty())
        return {};
    // Room around the area for it to grow into, and an empty border so that every outline closes.
    const int margin = int(std::ceil(std::max(0.0, pixels))) + 2;
    const QRect grid = bounds.adjusted(-margin, -margin, margin, margin);
    const int w = grid.width(), h = grid.height();
    const SelectionMask mask = SelectionMask::fromPath(area);
    std::vector<std::uint8_t> inside(std::size_t(w) * std::size_t(h), 0), outside(inside.size(), 1);
    for (int y = 0; y < mask.bounds.height(); ++y) {
        const uchar *coverage = mask.coverage.constScanLine(y);
        for (int x = 0; x < mask.bounds.width(); ++x) {
            const std::size_t i = std::size_t(y + mask.bounds.y() - grid.y()) * std::size_t(w)
                                  + std::size_t(x + mask.bounds.x() - grid.x());
            inside[i] = coverage[x] >= 128;
            outside[i] = !inside[i];
        }
    }
    // A signed distance to the edge, which runs half a pixel out of the pixels inside: negative inside. The new edge is
    // where it equals `pixels`.
    const std::vector<float> toInside = distanceTo(inside, w, h), toOutside = distanceTo(outside, w, h);
    std::vector<float> field(inside.size());
    for (std::size_t i = 0; i < field.size(); ++i)
        field[i] = (inside[i] ? 0.5f - toOutside[i] : toInside[i] - 0.5f) - float(pixels);
    for (int x = 0; x < w; ++x) { // the border stays outside, whatever the distance says
        field[std::size_t(x)] = field[std::size_t(h - 1) * std::size_t(w) + std::size_t(x)] = 1.0f;
    }
    for (int y = 0; y < h; ++y)
        field[std::size_t(y) * std::size_t(w)] = field[std::size_t(y) * std::size_t(w) + std::size_t(w - 1)] = 1.0f;

    // Marching squares over the cells between pixel centers: inside where the field is below 0. Each crossing sits on
    // a cell edge, named by an id; each segment goes from one crossing to the next with the inside on its left, so
    // every crossing starts exactly one segment and the segments chain into closed outlines.
    auto at = [&](int x, int y) { return field[std::size_t(y) * std::size_t(w) + std::size_t(x)]; };
    // Edge ids: horizontal edge from (x, y) to (x + 1, y) is 2·(y·w + x); vertical from (x, y) to (x, y + 1) is that + 1.
    auto horizontal = [&](int x, int y) { return 2 * (std::int64_t(y) * w + x); };
    auto vertical = [&](int x, int y) { return 2 * (std::int64_t(y) * w + x) + 1; };
    auto crossing = [&](std::int64_t edge) {
        const std::int64_t cell = edge / 2;
        const int x = int(cell % w), y = int(cell / w);
        const float a = at(x, y), b = (edge % 2) ? at(x, y + 1) : at(x + 1, y);
        const double t = std::clamp(double(a) / double(a - b), 0.0, 1.0);
        // Pixel (x, y) has its center at (grid.x + x + 0.5, grid.y + y + 0.5).
        return (edge % 2) ? QPointF(grid.x() + x + 0.5, grid.y() + y + 0.5 + t)
                          : QPointF(grid.x() + x + 0.5 + t, grid.y() + y + 0.5);
    };
    std::unordered_map<std::int64_t, std::int64_t> next; // crossing -> the following crossing
    next.reserve(std::size_t(w + h) * 4);
    for (int y = 0; y + 1 < h; ++y)
        for (int x = 0; x + 1 < w; ++x) {
            const float tl = at(x, y), tr = at(x + 1, y), br = at(x + 1, y + 1), bl = at(x, y + 1);
            const int index = (tl < 0) << 3 | (tr < 0) << 2 | (br < 0) << 1 | (bl < 0);
            if (index == 0 || index == 15)
                continue;
            const std::int64_t top = horizontal(x, y), bottom = horizontal(x, y + 1), left = vertical(x, y),
                               right = vertical(x + 1, y);
            // In y-down coordinates "inside on the left" of a segment a -> b; the cases list them that way.
            auto link = [&](std::int64_t from, std::int64_t to) { next[from] = to; };
            switch (index) {
            case 1: link(bottom, left); break;   // bl
            case 2: link(right, bottom); break;  // br
            case 3: link(right, left); break;    // bl br
            case 4: link(top, right); break;     // tr
            case 6: link(top, bottom); break;    // tr br
            case 7: link(top, left); break;      // tr br bl
            case 8: link(left, top); break;      // tl
            case 9: link(bottom, top); break;    // tl bl
            case 11: link(right, top); break;    // tl bl br
            case 12: link(left, right); break;   // tl tr
            case 13: link(bottom, right); break; // tl tr bl
            case 14: link(left, bottom); break;  // tl tr br
            case 5:                              // tr bl: joined through the middle when it is inside
            case 10: {                           // tl br
                const bool middle = (tl + tr + br + bl) < 0;
                if (index == 5) {
                    if (middle) {
                        link(top, left);
                        link(bottom, right);
                    } else {
                        link(top, right);
                        link(bottom, left);
                    }
                } else {
                    if (middle) {
                        link(left, bottom);
                        link(right, top);
                    } else {
                        link(left, top);
                        link(right, bottom);
                    }
                }
                break;
            }
            }
        }

    QPainterPath result;
    result.setFillRule(Qt::WindingFill);
    while (!next.empty()) {
        const std::int64_t start = next.begin()->first;
        std::vector<puppet::Vec2> loop;
        std::int64_t edge = start;
        do {
            const QPointF p = crossing(edge);
            loop.push_back({p.x(), p.y()});
            const auto it = next.find(edge);
            if (it == next.end())
                break; // cannot happen: every crossing starts a segment
            edge = it->second;
            next.erase(it);
        } while (edge != start);
        if (loop.size() < 3)
            continue;
        loop = puppet::simplifyContour(loop, 0.2);
        QPolygonF polygon;
        polygon.reserve(qsizetype(loop.size()) + 1);
        for (const puppet::Vec2 &p : loop)
            polygon << QPointF(p.x, p.y);
        polygon << polygon.front();
        result.addPolygon(polygon);
        result.closeSubpath();
    }
    return result;
}

} // namespace mothion::canvas
