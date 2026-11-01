#include "core/puppet/WarpRaster.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace mothion::puppet {

namespace {

// p + (q − p)·f/256 on all four 8-bit channels, two at a time.
inline std::uint32_t lerp(std::uint32_t p, std::uint32_t q, std::uint32_t f)
{
    const std::uint32_t g = 256 - f;
    const std::uint32_t rb = (((p & 0xFF00FFu) * g + (q & 0xFF00FFu) * f) >> 8) & 0xFF00FFu;
    const std::uint32_t ag = ((((p >> 8) & 0xFF00FFu) * g + ((q >> 8) & 0xFF00FFu) * f) >> 8) & 0xFF00FFu;
    return rb | (ag << 8);
}

// Bilinear sample at document point (u, v); pixel centers sit at +0.5.
inline std::uint32_t sampleBilinear(const RasterImage &src, double u, double v)
{
    const double sx = u - src.x - 0.5, sy = v - src.y - 0.5;
    const int x0 = int(std::floor(sx)), y0 = int(std::floor(sy));
    const std::uint32_t fx = std::uint32_t((sx - x0) * 256.0), fy = std::uint32_t((sy - y0) * 256.0);
    std::uint32_t p00, p10, p01, p11;
    if (x0 >= 0 && y0 >= 0 && x0 + 1 < src.width && y0 + 1 < src.height) {
        const std::uint32_t *row = src.pixels.data() + std::size_t(y0) * std::size_t(src.width) + std::size_t(x0);
        p00 = row[0], p10 = row[1];
        p01 = row[src.width], p11 = row[src.width + 1];
    } else {
        auto at = [&](int x, int y) -> std::uint32_t {
            if (x < 0 || y < 0 || x >= src.width || y >= src.height)
                return 0;
            return src.pixels[std::size_t(y) * std::size_t(src.width) + std::size_t(x)];
        };
        p00 = at(x0, y0), p10 = at(x0 + 1, y0), p01 = at(x0, y0 + 1), p11 = at(x0 + 1, y0 + 1);
    }
    if ((p00 | p10 | p01 | p11) == 0)
        return 0;
    return lerp(lerp(p00, p10, fx), lerp(p01, p11, fx), fy);
}

inline std::uint32_t over(std::uint32_t dst, std::uint32_t src)
{
    const std::uint32_t a = src >> 24;
    if (a == 255 || dst == 0)
        return src;
    if (a == 0)
        return dst;
    // src + dst·(255 − a)/255, with the 256-based lerp as an approximation.
    return src + lerp(0, dst, 256 - (a + (a >> 7)));
}

// Pixels of `target` in rows [rowBegin, rowEnd) whose centers fall in the
// triangle (d0, d1, d2), each filled from the source point the same
// barycentric coordinates give in (s0, s1, s2). Along a row the source
// point moves by a constant step: one addition per pixel.
void drawTriangle(const RasterImage &source, RasterImage &target, int rowBegin, int rowEnd, Vec2 d0, Vec2 d1, Vec2 d2,
                  Vec2 s0, Vec2 s1, Vec2 s2)
{
    const double area = orient(d0, d1, d2);
    if (std::abs(area) < 1e-9)
        return;
    const int y0 = std::max({rowBegin, target.y, int(std::floor(std::min({d0.y, d1.y, d2.y})))});
    const int y1 = std::min({rowEnd - 1, target.y + target.height - 1, int(std::ceil(std::max({d0.y, d1.y, d2.y})))});
    if (y0 > y1)
        return;

    // Edge functions e_i(p) = A_i·x + B_i·y + C_i, ≥ 0 inside (sign-corrected
    // for folded triangles); barycentric weight w_i = e_i / |area|.
    const Vec2 d[3] = {d0, d1, d2};
    double A[3], B[3], C[3];
    const double sign = area > 0 ? 1.0 : -1.0;
    for (int i = 0; i < 3; ++i) {
        const Vec2 a = d[(i + 1) % 3], b = d[(i + 2) % 3];
        A[i] = sign * (a.y - b.y);
        B[i] = sign * (b.x - a.x);
        C[i] = sign * (a.x * b.y - a.y * b.x);
    }
    const double inv = 1.0 / std::abs(area);
    // Source point as an affine function of the destination pixel.
    const double ux = (A[0] * s0.x + A[1] * s1.x + A[2] * s2.x) * inv;
    const double uy = (B[0] * s0.x + B[1] * s1.x + B[2] * s2.x) * inv;
    const double uc = (C[0] * s0.x + C[1] * s1.x + C[2] * s2.x) * inv;
    const double vx = (A[0] * s0.y + A[1] * s1.y + A[2] * s2.y) * inv;
    const double vy = (B[0] * s0.y + B[1] * s1.y + B[2] * s2.y) * inv;
    const double vc = (C[0] * s0.y + C[1] * s1.y + C[2] * s2.y) * inv;

    for (int y = y0; y <= y1; ++y) {
        const double py = y + 0.5;
        // The span of the row inside all three edges: [left, right).
        double left = -1e300, right = 1e300;
        bool empty = false;
        for (int i = 0; i < 3; ++i) {
            const double k = B[i] * py + C[i];
            if (A[i] > 0)
                left = std::max(left, -k / A[i]);
            else if (A[i] < 0)
                right = std::min(right, -k / A[i]);
            else if (k < 0)
                empty = true;
        }
        if (empty || left >= right)
            continue;
        const int xs = std::max(target.x, int(std::ceil(left - 0.5)));
        const int xe = std::min(target.x + target.width, int(std::ceil(right - 0.5)));
        if (xs >= xe)
            continue;
        double u = ux * (xs + 0.5) + uy * py + uc;
        double v = vx * (xs + 0.5) + vy * py + vc;
        std::uint32_t *out = target.pixels.data() + std::size_t(y - target.y) * std::size_t(target.width);
        for (int x = xs; x < xe; ++x, u += ux, v += vx) {
            const std::uint32_t c = sampleBilinear(source, u, v);
            if (c != 0) {
                std::uint32_t &dst = out[x - target.x];
                dst = over(dst, c);
            }
        }
    }
}

} // namespace

void deformedBounds(const PuppetRig &rig, int &x, int &y, int &width, int &height)
{
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (const auto &mesh : rig.meshes())
        for (const Vec2 &p : mesh.deformed) {
            minX = std::min(minX, p.x), maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
        }
    if (minX > maxX) {
        x = y = width = height = 0;
        return;
    }
    x = int(std::floor(minX)) - 1;
    y = int(std::floor(minY)) - 1;
    width = int(std::ceil(maxX)) + 2 - x;
    height = int(std::ceil(maxY)) + 2 - y;
}

void warp(const PuppetRig &rig, const RasterImage &source, RasterImage &target)
{
    if (target.isEmpty())
        return;
    // Horizontal bands drawn in parallel; within a band, triangles keep their order.
    auto band = [&](int rowBegin, int rowEnd) {
        for (const auto &mesh : rig.meshes())
            for (const auto &t : mesh.rest.triangles)
                drawTriangle(source, target, rowBegin, rowEnd, mesh.deformed[std::size_t(t[0])],
                             mesh.deformed[std::size_t(t[1])], mesh.deformed[std::size_t(t[2])],
                             mesh.rest.vertices[std::size_t(t[0])], mesh.rest.vertices[std::size_t(t[1])],
                             mesh.rest.vertices[std::size_t(t[2])]);
    };
    const int bands = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
    const int rows = (target.height + bands - 1) / bands;
    if (bands == 1 || target.height < 64) {
        band(target.y, target.y + target.height);
        return;
    }
    std::vector<std::thread> workers;
    for (int b = 1; b < bands; ++b) {
        const int begin = target.y + b * rows, end = std::min(target.y + target.height, begin + rows);
        if (begin < end)
            workers.emplace_back(band, begin, end);
    }
    band(target.y, target.y + rows);
    for (std::thread &w : workers)
        w.join();
}

} // namespace mothion::puppet
