#include "core/puppet/ArapDeformer.h"

#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>

#include <algorithm>
#include <cmath>
#include <map>

namespace mothion::puppet {

struct ArapDeformer::Impl
{
    std::vector<Vec2> rest;
    // Undirected edges with their cotangent weights (clamped positive so
    // obtuse triangles cannot make the system indefinite).
    struct Edge
    {
        int i, j;
        double w;
    };
    std::vector<Edge> edges;
    std::vector<std::vector<std::pair<int, double>>> neighbours; // per vertex: (j, w_ij)

    std::vector<int> handles;
    std::vector<int> freeIndex; // vertex → row among the free ones, or -1
    std::vector<int> freeVertices;
    Eigen::SparseMatrix<double> coupling; // free rows × handle columns (−w)
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
    bool ready = false;
};

ArapDeformer::ArapDeformer(const TriMesh &rest)
    : d(std::make_unique<Impl>())
{
    d->rest = rest.vertices;
    std::map<std::pair<int, int>, double> weights;
    for (const auto &t : rest.triangles) {
        for (int k = 0; k < 3; ++k) {
            const int a = t[std::size_t(k)], b = t[std::size_t((k + 1) % 3)], c = t[std::size_t((k + 2) % 3)];
            // Angle at c faces edge (a, b).
            const Vec2 u = rest.vertices[std::size_t(a)] - rest.vertices[std::size_t(c)];
            const Vec2 v = rest.vertices[std::size_t(b)] - rest.vertices[std::size_t(c)];
            const double cot = dot(u, v) / std::max(std::abs(cross(u, v)), 1e-12);
            weights[{std::min(a, b), std::max(a, b)}] += 0.5 * cot;
        }
    }
    d->neighbours.resize(rest.vertices.size());
    for (const auto &[e, w] : weights) {
        const double weight = std::max(w, 1e-3);
        d->edges.push_back({e.first, e.second, weight});
        d->neighbours[std::size_t(e.first)].emplace_back(e.second, weight);
        d->neighbours[std::size_t(e.second)].emplace_back(e.first, weight);
    }
}

ArapDeformer::~ArapDeformer() = default;

void ArapDeformer::setHandles(const std::vector<int> &vertices)
{
    const int n = int(d->rest.size());
    d->handles = vertices;
    d->freeIndex.assign(std::size_t(n), -1);
    d->freeVertices.clear();
    std::vector<int> handleIndex(std::size_t(n), -1);
    for (std::size_t h = 0; h < vertices.size(); ++h)
        handleIndex[std::size_t(vertices[h])] = int(h);
    for (int v = 0; v < n; ++v)
        if (handleIndex[std::size_t(v)] < 0) {
            d->freeIndex[std::size_t(v)] = int(d->freeVertices.size());
            d->freeVertices.push_back(v);
        }
    d->ready = false;
    if (vertices.empty() || d->freeVertices.empty())
        return;

    // Laplacian restricted to the free vertices, and their coupling to the handles.
    const int nf = int(d->freeVertices.size());
    std::vector<Eigen::Triplet<double>> lff, lfh;
    for (int f = 0; f < nf; ++f) {
        const int v = d->freeVertices[std::size_t(f)];
        double diagonal = 0.0;
        for (const auto &[j, w] : d->neighbours[std::size_t(v)]) {
            diagonal += w;
            if (const int fj = d->freeIndex[std::size_t(j)]; fj >= 0)
                lff.emplace_back(f, fj, -w);
            else
                lfh.emplace_back(f, handleIndex[std::size_t(j)], -w);
        }
        lff.emplace_back(f, f, diagonal);
    }
    Eigen::SparseMatrix<double> a(nf, nf);
    a.setFromTriplets(lff.begin(), lff.end());
    d->coupling.resize(nf, int(vertices.size()));
    d->coupling.setFromTriplets(lfh.begin(), lfh.end());
    d->solver.compute(a);
    // A part of the mesh with no handle in it has no unique solution: those
    // vertices simply keep their place (the system is then not definite).
    d->ready = d->solver.info() == Eigen::Success;
}

void ArapDeformer::solve(std::vector<Vec2> &positions, const std::vector<Vec2> &targets, int iterations) const
{
    const std::size_t n = d->rest.size();
    if (positions.size() != n || targets.size() != d->handles.size())
        return;
    for (std::size_t h = 0; h < d->handles.size(); ++h)
        positions[std::size_t(d->handles[h])] = targets[h];
    if (!d->ready)
        return;

    const int nf = int(d->freeVertices.size());
    Eigen::VectorXd hx(int(targets.size())), hy(int(targets.size()));
    for (std::size_t h = 0; h < targets.size(); ++h) {
        hx[int(h)] = targets[h].x;
        hy[int(h)] = targets[h].y;
    }
    const Eigen::VectorXd fixedX = d->coupling * hx, fixedY = d->coupling * hy;

    std::vector<double> cosR(n), sinR(n);
    Eigen::VectorXd bx(nf), by(nf);
    for (int it = 0; it < iterations; ++it) {
        // Local step: best rotation per vertex, θ = atan2(M10 − M01, M00 + M11)
        // for M = Σ w (p'i − p'j)(pi − pj)ᵀ.
        for (std::size_t v = 0; v < n; ++v) {
            double m00 = 0, m01 = 0, m10 = 0, m11 = 0;
            for (const auto &[j, w] : d->neighbours[v]) {
                const Vec2 e = d->rest[v] - d->rest[std::size_t(j)];
                const Vec2 f = positions[v] - positions[std::size_t(j)];
                m00 += w * f.x * e.x;
                m01 += w * f.x * e.y;
                m10 += w * f.y * e.x;
                m11 += w * f.y * e.y;
            }
            const double angle = std::atan2(m10 - m01, m00 + m11);
            cosR[v] = std::cos(angle);
            sinR[v] = std::sin(angle);
        }
        // Global step: L p' = Σ w/2 (Ri + Rj)(pi − pj), handles moved to the right side.
        for (int f = 0; f < nf; ++f) {
            const std::size_t v = std::size_t(d->freeVertices[std::size_t(f)]);
            double sx = 0.0, sy = 0.0;
            for (const auto &[j, w] : d->neighbours[v]) {
                const Vec2 e = d->rest[v] - d->rest[std::size_t(j)];
                const double c = 0.5 * (cosR[v] + cosR[std::size_t(j)]);
                const double s = 0.5 * (sinR[v] + sinR[std::size_t(j)]);
                sx += w * (c * e.x - s * e.y);
                sy += w * (s * e.x + c * e.y);
            }
            bx[f] = sx - fixedX[f];
            by[f] = sy - fixedY[f];
        }
        const Eigen::VectorXd x = d->solver.solve(bx), y = d->solver.solve(by);
        for (int f = 0; f < nf; ++f)
            positions[std::size_t(d->freeVertices[std::size_t(f)])] = {x[f], y[f]};
    }
}

} // namespace mothion::puppet
