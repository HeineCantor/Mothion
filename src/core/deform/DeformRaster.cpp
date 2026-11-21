#include "core/deform/DeformRaster.h"

#include <algorithm>
#include <cmath>

namespace mothion::deform {

puppet::RasterImage warpImage(const puppet::RasterImage &source, const std::function<Vec2(Vec2)> &map, int cell)
{
    if (source.isEmpty())
        return {};
    cell = std::max(1, cell);
    const int columns = (source.width + cell - 1) / cell, rows = (source.height + cell - 1) / cell;
    // Which cells have pixels (a pixel's neighbours too: the sampling reads a pixel round each point).
    std::vector<char> full(std::size_t(columns) * std::size_t(rows), 0);
    for (int y = 0; y < source.height; ++y) {
        const std::uint32_t *row = source.pixels.data() + std::size_t(y) * std::size_t(source.width);
        for (int x = 0; x < source.width; ++x)
            if (row[x] != 0)
                for (int cy = std::max(0, (y - 1) / cell); cy <= std::min(rows - 1, (y + 1) / cell); ++cy)
                    for (int cx = std::max(0, (x - 1) / cell); cx <= std::min(columns - 1, (x + 1) / cell); ++cx)
                        full[std::size_t(cy) * std::size_t(columns) + std::size_t(cx)] = 1;
    }
    // The grid's vertices, only those of the cells with pixels moved.
    const int across = columns + 1;
    std::vector<Vec2> rest(std::size_t(across) * std::size_t(rows + 1)), moved(rest.size());
    std::vector<char> used(rest.size(), 0);
    std::vector<std::array<int, 3>> triangles;
    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < columns; ++cx) {
            if (!full[std::size_t(cy) * std::size_t(columns) + std::size_t(cx)])
                continue;
            const int v00 = cy * across + cx, v10 = v00 + 1, v01 = v00 + across, v11 = v01 + 1;
            for (int v : {v00, v10, v01, v11})
                used[std::size_t(v)] = 1;
            triangles.push_back({v00, v10, v11});
            triangles.push_back({v00, v11, v01});
        }
    if (triangles.empty())
        return {};
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (int vy = 0; vy <= rows; ++vy)
        for (int vx = 0; vx <= columns; ++vx) {
            const std::size_t v = std::size_t(vy) * std::size_t(across) + std::size_t(vx);
            if (!used[v])
                continue;
            rest[v] = {double(source.x + std::min(vx * cell, source.width)), double(source.y + std::min(vy * cell, source.height))};
            moved[v] = map(rest[v]);
            minX = std::min(minX, moved[v].x), maxX = std::max(maxX, moved[v].x);
            minY = std::min(minY, moved[v].y), maxY = std::max(maxY, moved[v].y);
        }
    const int x0 = int(std::floor(minX)) - 1, y0 = int(std::floor(minY)) - 1;
    puppet::RasterImage target(x0, y0, int(std::ceil(maxX)) + 2 - x0, int(std::ceil(maxY)) + 2 - y0);
    puppet::warpTriangles(rest, moved, triangles, source, target);
    return target;
}

} // namespace mothion::deform
