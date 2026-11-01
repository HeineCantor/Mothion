#include "core/puppet/Contour.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <unordered_map>

namespace mothion::puppet {

namespace {

// The 8 neighbours, clockwise on screen (y down), starting east.
constexpr std::array<int, 8> kDx{1, 1, 0, -1, -1, -1, 0, 1};
constexpr std::array<int, 8> kDy{0, 1, 1, 1, 0, -1, -1, -1};

int directionOf(int dx, int dy)
{
    for (int d = 0; d < 8; ++d)
        if (kDx[std::size_t(d)] == dx && kDy[std::size_t(d)] == dy)
            return d;
    return 0;
}

// Indices of the edges (i → i+1) that cross a non-adjacent edge.
std::vector<std::size_t> crossingEdges(const std::vector<Vec2> &poly, double cellSize)
{
    const std::size_t n = poly.size();
    std::vector<std::size_t> bad;
    if (n < 4)
        return bad;
    auto key = [](long long cx, long long cy) { return (cx << 32) ^ (cy & 0xffffffffLL); };
    std::unordered_map<long long, std::vector<std::size_t>> grid;
    auto cells = [&](std::size_t e, auto &&visit) {
        const Vec2 a = poly[e], b = poly[(e + 1) % n];
        const long long x0 = (long long)std::floor(std::min(a.x, b.x) / cellSize);
        const long long x1 = (long long)std::floor(std::max(a.x, b.x) / cellSize);
        const long long y0 = (long long)std::floor(std::min(a.y, b.y) / cellSize);
        const long long y1 = (long long)std::floor(std::max(a.y, b.y) / cellSize);
        for (long long cy = y0; cy <= y1; ++cy)
            for (long long cx = x0; cx <= x1; ++cx)
                visit(key(cx, cy));
    };
    for (std::size_t e = 0; e < n; ++e)
        cells(e, [&](long long k) { grid[k].push_back(e); });
    std::vector<char> marked(n, 0);
    for (std::size_t e = 0; e < n; ++e) {
        cells(e, [&](long long k) {
            for (std::size_t f : grid[k]) {
                if (f <= e || f == e + 1 || (e == 0 && f == n - 1))
                    continue; // adjacent edges share an end point
                if (marked[e] && marked[f])
                    continue;
                if (segmentsIntersect(poly[e], poly[(e + 1) % n], poly[f], poly[(f + 1) % n])) {
                    marked[e] = marked[f] = 1;
                }
            }
        });
    }
    for (std::size_t e = 0; e < n; ++e)
        if (marked[e])
            bad.push_back(e);
    return bad;
}

} // namespace

bool Bitmask::isEmpty() const
{
    return std::none_of(bits.begin(), bits.end(), [](std::uint8_t b) { return b != 0; });
}

Bitmask dilated(const Bitmask &mask, int radius)
{
    if (radius <= 0)
        return mask;
    Bitmask out(mask.originX - radius, mask.originY - radius, mask.width + 2 * radius, mask.height + 2 * radius);
    // Chamfer distance (3-4) to the nearest shape pixel, then threshold.
    const int w = out.width, h = out.height;
    constexpr int kFar = 1 << 28;
    std::vector<int> dist(std::size_t(w) * std::size_t(h), kFar);
    auto at = [&](int x, int y) -> int & { return dist[std::size_t(y) * std::size_t(w) + std::size_t(x)]; };
    for (int y = 0; y < mask.height; ++y)
        for (int x = 0; x < mask.width; ++x)
            if (mask.at(x, y))
                at(x + radius, y + radius) = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int &d = at(x, y);
            if (x > 0) d = std::min(d, at(x - 1, y) + 3);
            if (y > 0) d = std::min(d, at(x, y - 1) + 3);
            if (x > 0 && y > 0) d = std::min(d, at(x - 1, y - 1) + 4);
            if (x + 1 < w && y > 0) d = std::min(d, at(x + 1, y - 1) + 4);
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            int &d = at(x, y);
            if (x + 1 < w) d = std::min(d, at(x + 1, y) + 3);
            if (y + 1 < h) d = std::min(d, at(x, y + 1) + 3);
            if (x + 1 < w && y + 1 < h) d = std::min(d, at(x + 1, y + 1) + 4);
            if (x > 0 && y + 1 < h) d = std::min(d, at(x - 1, y + 1) + 4);
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out.set(x, y, at(x, y) <= 3 * radius);
    return out;
}

std::vector<std::vector<Vec2>> outerContours(const Bitmask &mask)
{
    std::vector<std::vector<Vec2>> contours;
    std::vector<std::uint8_t> done(mask.bits.size(), 0); // pixels of parts already traced
    auto index = [&](int x, int y) { return std::size_t(y) * std::size_t(mask.width) + std::size_t(x); };
    auto toDocument = [&](int x, int y) { return Vec2{mask.originX + x + 0.5, mask.originY + y + 0.5}; };

    for (int sy = 0; sy < mask.height; ++sy)
        for (int sx = 0; sx < mask.width; ++sx) {
            if (!mask.at(sx, sy) || done[index(sx, sy)])
                continue;
            // (sx, sy) is the top-left pixel of a new part: its west neighbour is empty.
            std::vector<Vec2> contour{toDocument(sx, sy)};
            int px = sx, py = sy;
            int back = 4; // direction from the current pixel to the background pixel we came from
            int firstX = -1, firstY = -1;
            for (std::size_t steps = 0; steps < mask.bits.size() * 4; ++steps) {
                int found = -1;
                for (int k = 1; k <= 8; ++k) {
                    const int d = (back + k) % 8;
                    if (mask.at(px + kDx[std::size_t(d)], py + kDy[std::size_t(d)])) {
                        found = d;
                        break;
                    }
                }
                if (found < 0)
                    break; // a single pixel
                // The pixel checked just before `found` was background: it is the new back.
                const int prev = (found + 7) % 8;
                const int bx = px + kDx[std::size_t(prev)], by = py + kDy[std::size_t(prev)];
                const int nx = px + kDx[std::size_t(found)], ny = py + kDy[std::size_t(found)];
                if (px == sx && py == sy) {
                    if (firstX < 0) {
                        firstX = nx;
                        firstY = ny;
                    } else if (nx == firstX && ny == firstY) {
                        break; // back at the start, going the same way: closed
                    }
                }
                back = directionOf(bx - nx, by - ny);
                px = nx;
                py = ny;
                if (!(px == sx && py == sy) || contour.size() == 1)
                    contour.push_back(toDocument(px, py));
            }
            if (contour.size() > 1 && contour.back() == contour.front())
                contour.pop_back();

            // Mark the whole part (8-connected) so it is not traced again.
            std::deque<std::pair<int, int>> queue{{sx, sy}};
            done[index(sx, sy)] = 1;
            while (!queue.empty()) {
                const auto [x, y] = queue.front();
                queue.pop_front();
                for (int d = 0; d < 8; ++d) {
                    const int qx = x + kDx[std::size_t(d)], qy = y + kDy[std::size_t(d)];
                    if (mask.at(qx, qy) && !done[index(qx, qy)]) {
                        done[index(qx, qy)] = 1;
                        queue.emplace_back(qx, qy);
                    }
                }
            }
            contours.push_back(std::move(contour));
        }
    return contours;
}

std::vector<Vec2> simplifyContour(const std::vector<Vec2> &contour)
{
    std::vector<Vec2> points;
    for (const Vec2 &p : contour)
        if (points.empty() || !(p == points.back()))
            points.push_back(p);
    while (points.size() > 1 && points.back() == points.front())
        points.pop_back();
    const std::size_t n = points.size();
    if (n < 4)
        return points;

    double perimeter = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        perimeter += length(points[(i + 1) % n] - points[i]);
    const double minLength = std::max(perimeter / 1000.0, std::sqrt(2.0));

    // Greedy pass by distance, remembering which original points are kept.
    std::vector<char> keep(n, 0);
    keep[0] = 1;
    Vec2 last = points[0];
    for (std::size_t i = 1; i < n; ++i)
        if (length(points[i] - last) >= minLength && length(points[0] - points[i]) >= minLength * 0.5) {
            keep[i] = 1;
            last = points[i];
        }

    // Where the simplified outline crosses itself, put the original points
    // of the offending edges back, and check again.
    const double cell = std::max(8.0, 4.0 * minLength);
    for (int round = 0; round < 32; ++round) {
        std::vector<std::size_t> kept;
        for (std::size_t i = 0; i < n; ++i)
            if (keep[i])
                kept.push_back(i);
        std::vector<Vec2> poly;
        for (std::size_t i : kept)
            poly.push_back(points[i]);
        const std::vector<std::size_t> bad = crossingEdges(poly, cell);
        if (bad.empty())
            return poly;
        for (std::size_t e : bad) {
            const std::size_t from = kept[e], to = e + 1 < kept.size() ? kept[e + 1] : kept[0] + n;
            for (std::size_t i = from + 1; i < to; ++i)
                keep[i % n] = 1;
        }
    }
    return points; // could not simplify safely
}

} // namespace mothion::puppet
