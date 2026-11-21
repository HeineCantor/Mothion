#include "core/fill/FillShape.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <queue>

namespace mothion::fill {

namespace {

int difference(doc::Pixel a, doc::Pixel b)
{
    int most = 0;
    for (int shift = 0; shift < 32; shift += 8)
        most = std::max(most, std::abs(int((a >> shift) & 0xff) - int((b >> shift) & 0xff)));
    return most;
}

} // namespace

Region grownToLineMiddle(const Region &region, const doc::Pixel *pixels, doc::Pixel seed, int tolerance, int reach)
{
    Region out = region;
    if (region.isEmpty() || !pixels)
        return out;
    const int w = region.width, h = region.height;
    const std::size_t n = std::size_t(w) * std::size_t(h);
    // Only the area round the region, `reach` beyond it, is looked at.
    const int x0 = std::max(0, region.left - reach - 1), x1 = std::min(w - 1, region.right + reach + 1);
    const int y0 = std::max(0, region.top - reach - 1), y1 = std::min(h - 1, region.bottom + reach + 1);
    constexpr int kFar = std::numeric_limits<int>::max();
    std::vector<int> fromInside(n, kFar), fromBeyond(n, kFar);
    auto index = [w](int x, int y) { return std::size_t(y) * std::size_t(w) + std::size_t(x); };
    auto isLine = [&](std::size_t i) { return !region.mask[i] && difference(pixels[i], seed) > tolerance; };
    // Steps of 2 straight and 3 diagonal (close to round), from each side over the line's pixels.
    // Steps of 2 straight and 3 diagonal (close to round), from each side over the line's pixels, nearest first.
    auto spread = [&](std::vector<int> &distance, bool inside) {
        using Item = std::pair<int, std::size_t>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> queue;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const std::size_t i = index(x, y);
                const bool start = inside ? region.mask[i] != 0 : (!region.mask[i] && !isLine(i));
                if (start) {
                    distance[i] = 0;
                    queue.emplace(0, i);
                }
            }
        while (!queue.empty()) {
            const auto [d0, i] = queue.top();
            queue.pop();
            if (d0 > distance[i])
                continue;
            const int x = int(i % std::size_t(w)), y = int(i / std::size_t(w));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy)
                        continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < x0 || ny < y0 || nx > x1 || ny > y1)
                        continue;
                    const std::size_t j = index(nx, ny);
                    if (!isLine(j))
                        continue;
                    const int d = d0 + (dx && dy ? 3 : 2);
                    if (d < distance[j] && d <= 2 * reach) {
                        distance[j] = d;
                        queue.emplace(d, j);
                    }
                }
        }
    };
    spread(fromInside, true);
    spread(fromBeyond, false);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const std::size_t i = index(x, y);
            if (fromInside[i] == kFar || out.mask[i] || fromInside[i] > fromBeyond[i])
                continue;
            out.mask[i] = 255;
            out.left = std::min(out.left, x);
            out.right = std::max(out.right, x);
            out.top = std::min(out.top, y);
            out.bottom = std::max(out.bottom, y);
        }
    return out;
}

std::vector<std::vector<Point>> regionOutlines(const Region &region)
{
    std::vector<std::vector<Point>> outlines;
    if (region.isEmpty())
        return outlines;
    const int w = region.width, h = region.height;
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && region.at(x, y) >= 128; };
    // The edges between a pixel inside and one outside, each going with the inside on its right (y down): from a
    // corner to the next. A corner where two edges start (pixels touching only at a corner) keeps both.
    struct Edge
    {
        int x0, y0, x1, y1;
        bool used = false;
    };
    std::vector<Edge> edges;
    std::multimap<std::pair<int, int>, std::size_t> starting;
    auto add = [&](int ax, int ay, int bx, int by) {
        starting.emplace(std::pair{ax, ay}, edges.size());
        edges.push_back({ax, ay, bx, by});
    };
    for (int y = region.top; y <= region.bottom; ++y)
        for (int x = region.left; x <= region.right; ++x) {
            if (!inside(x, y))
                continue;
            if (!inside(x, y - 1))
                add(x, y, x + 1, y); // top, going right
            if (!inside(x + 1, y))
                add(x + 1, y, x + 1, y + 1); // right, going down
            if (!inside(x, y + 1))
                add(x + 1, y + 1, x, y + 1); // bottom, going left
            if (!inside(x - 1, y))
                add(x, y + 1, x, y); // left, going up
        }
    for (std::size_t first = 0; first < edges.size(); ++first) {
        if (edges[first].used)
            continue;
        std::vector<Point> loop;
        std::size_t e = first;
        while (!edges[e].used) {
            Edge &edge = edges[e];
            edge.used = true;
            loop.push_back({double(edge.x0), double(edge.y0)});
            // The next edge from where this one ends: at a corner with two, the one that turns right (it keeps the
            // pixels that touch only at a corner apart).
            const int dx = edge.x1 - edge.x0, dy = edge.y1 - edge.y0;
            std::size_t next = edges.size();
            int best = -2;
            auto [lo, hi] = starting.equal_range({edge.x1, edge.y1});
            for (auto it = lo; it != hi; ++it) {
                const Edge &candidate = edges[it->second];
                if (candidate.used && it->second != first)
                    continue;
                const int cx = candidate.x1 - candidate.x0, cy = candidate.y1 - candidate.y0;
                const int turn = dx * cy - dy * cx; // > 0: a right turn (y down)
                const int score = turn > 0 ? 1 : turn == 0 ? 0 : -1;
                if (score > best) {
                    best = score;
                    next = it->second;
                }
            }
            if (next == edges.size() || next == first)
                break;
            e = next;
        }
        // The turns only.
        std::vector<Point> turns;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Point &a = loop[(i + loop.size() - 1) % loop.size()], &b = loop[i], &c = loop[(i + 1) % loop.size()];
            if ((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x) != 0)
                turns.push_back(b);
        }
        if (turns.size() >= 3)
            outlines.push_back(std::move(turns));
    }
    return outlines;
}

} // namespace mothion::fill
