#include "core/puppet/Triangulation.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <utility>

namespace mothion::puppet {

namespace {

// Edge i of a triangle joins v[i+1] and v[i+2]; it is opposite v[i], and
// n[i] is the triangle on its other side (-1 = none), c[i] marks it as part
// of the outline (constrained: never flipped, never crossed by the walk).
struct Tri
{
    std::array<int, 3> v{};
    std::array<int, 3> n{-1, -1, -1};
    std::array<bool, 3> c{};
    bool alive = true;
};

constexpr int next(int i) { return (i + 1) % 3; }
constexpr int prev(int i) { return (i + 2) % 3; }

class Cdt
{
public:
    std::vector<Vec2> pts;
    std::vector<Tri> tris;

    enum class Where { Inside, OnEdge, OnVertex, Outside };
    struct Location
    {
        Where where = Where::Outside;
        int tri = -1;
        int index = -1; // edge (OnEdge, Outside) or vertex (OnVertex) index in `tri`
    };

    explicit Cdt(const std::vector<Vec2> &points)
    {
        // Super-triangle well outside every point.
        double minX = points[0].x, maxX = minX, minY = points[0].y, maxY = minY;
        for (const Vec2 &p : points) {
            minX = std::min(minX, p.x), maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
        }
        const double d = std::max({maxX - minX, maxY - minY, 1.0}) * 20.0;
        const Vec2 c{(minX + maxX) / 2.0, (minY + maxY) / 2.0};
        pts = {{c.x - d, c.y - d}, {c.x + d, c.y - d}, {c.x, c.y + d}};
        m_scale = d;
        Tri t;
        t.v = {0, 1, 2};
        tris.push_back(t);
    }

    static constexpr int kSuperVertices = 3;

    int edgeIndex(int t, int a, int b) const
    {
        const Tri &x = tris[std::size_t(t)];
        for (int i = 0; i < 3; ++i) {
            const int p = x.v[std::size_t(next(i))], q = x.v[std::size_t(prev(i))];
            if ((p == a && q == b) || (p == b && q == a))
                return i;
        }
        return -1;
    }

    int backIndex(int u, int t) const
    {
        for (int j = 0; j < 3; ++j)
            if (tris[std::size_t(u)].n[std::size_t(j)] == t)
                return j;
        return -1;
    }

    void replaceNeighbor(int u, int oldT, int newT)
    {
        if (u < 0)
            return;
        Tri &x = tris[std::size_t(u)];
        for (int j = 0; j < 3; ++j)
            if (x.n[std::size_t(j)] == oldT) {
                x.n[std::size_t(j)] = newT;
                return;
            }
    }

    Vec2 P(int v) const { return pts[std::size_t(v)]; }

    // Tolerance for "on the line": relative to the scene size.
    bool nearlyZero(double area, Vec2 a, Vec2 b) const { return std::abs(area) <= 1e-10 * length(b - a) * m_scale; }

    Location locate(Vec2 p, int start) const
    {
        int t = start;
        if (t < 0 || t >= int(tris.size()) || !tris[std::size_t(t)].alive)
            t = firstAlive();
        int salt = 0;
        for (std::size_t steps = 0; steps < tris.size() * 3 + 64; ++steps) {
            const Tri &x = tris[std::size_t(t)];
            bool moved = false;
            for (int k = 0; k < 3; ++k) {
                const int i = (k + salt) % 3; // vary the edge order so the walk cannot cycle
                const Vec2 a = P(x.v[std::size_t(next(i))]), b = P(x.v[std::size_t(prev(i))]);
                const double o = orient(a, b, p);
                if (o < 0 && !nearlyZero(o, a, b)) {
                    if (x.n[std::size_t(i)] < 0)
                        return {Where::Outside, t, i};
                    t = x.n[std::size_t(i)];
                    moved = true;
                    break;
                }
            }
            ++salt;
            if (moved)
                continue;
            for (int i = 0; i < 3; ++i)
                if (length2(P(x.v[std::size_t(i)]) - p) <= 1e-18 * m_scale * m_scale)
                    return {Where::OnVertex, t, i};
            for (int i = 0; i < 3; ++i) {
                const Vec2 a = P(x.v[std::size_t(next(i))]), b = P(x.v[std::size_t(prev(i))]);
                if (nearlyZero(orient(a, b, p), a, b))
                    return {Where::OnEdge, t, i};
            }
            return {Where::Inside, t, -1};
        }
        return {Where::Outside, -1, -1};
    }

    int firstAlive() const
    {
        for (std::size_t t = 0; t < tris.size(); ++t)
            if (tris[t].alive)
                return int(t);
        return -1;
    }

    // --- insertion -----------------------------------------------------------------

    // Inserts p; returns its vertex index (an existing one if p is already there),
    // or -1 if p is outside the triangulation.
    int insert(Vec2 p, int hint, std::vector<int> *touched = nullptr)
    {
        const Location loc = locate(p, hint);
        switch (loc.where) {
        case Where::OnVertex: return tris[std::size_t(loc.tri)].v[std::size_t(loc.index)];
        case Where::Outside: return -1;
        case Where::Inside: return splitInside(loc.tri, p, touched);
        case Where::OnEdge: return splitEdge(loc.tri, loc.index, p, touched);
        }
        return -1;
    }

    int addTri(const Tri &t)
    {
        tris.push_back(t);
        return int(tris.size()) - 1;
    }

    int splitInside(int t, Vec2 p, std::vector<int> *touched)
    {
        const int pi = int(pts.size());
        pts.push_back(p);
        const Tri old = tris[std::size_t(t)];
        const int a = old.v[0], b = old.v[1], c = old.v[2];
        const int t1 = addTri({}), t2 = addTri({});
        Tri &T0 = tris[std::size_t(t)];
        T0 = {{pi, b, c}, {old.n[0], t1, t2}, {old.c[0], false, false}, true};
        tris[std::size_t(t1)] = {{a, pi, c}, {t, old.n[1], t2}, {false, old.c[1], false}, true};
        tris[std::size_t(t2)] = {{a, b, pi}, {t, t1, old.n[2]}, {false, false, old.c[2]}, true};
        replaceNeighbor(old.n[1], t, t1);
        replaceNeighbor(old.n[2], t, t2);
        m_last = t;
        legalize(t, 0, touched);
        legalize(t1, 1, touched);
        legalize(t2, 2, touched);
        if (touched)
            touched->insert(touched->end(), {t, t1, t2});
        return pi;
    }

    // Splits edge i of t (and the triangle across it) at p, which lies on it.
    int splitEdge(int t, int i, Vec2 p, std::vector<int> *touched)
    {
        const int pi = int(pts.size());
        pts.push_back(p);
        const Tri old = tris[std::size_t(t)];
        const int c = old.v[std::size_t(i)], a = old.v[std::size_t(next(i))], b = old.v[std::size_t(prev(i))];
        const int tA = old.n[std::size_t(next(i))]; // across (b, c)
        const int tB = old.n[std::size_t(prev(i))]; // across (c, a)
        const bool cA = old.c[std::size_t(next(i))], cB = old.c[std::size_t(prev(i))];
        const bool split = old.c[std::size_t(i)];
        const int u = old.n[std::size_t(i)];

        const int T1 = t, T2 = addTri({});
        int U1 = -1, U2 = -1;
        if (u >= 0) {
            U1 = u;
            U2 = addTri({});
        }
        tris[std::size_t(T1)] = {{c, a, pi}, {U2, T2, tB}, {split, false, cB}, true};
        tris[std::size_t(T2)] = {{c, pi, b}, {U1, tA, T1}, {split, cA, false}, true};
        replaceNeighbor(tA, t, T2);
        if (u >= 0) {
            const Tri uo = tris[std::size_t(u)];
            const int j = backIndex(u, t);
            const int d = uo.v[std::size_t(j)]; // u = (d, b, a)
            const int uA = uo.n[std::size_t(next(j))]; // across (a, d)
            const int uB = uo.n[std::size_t(prev(j))]; // across (d, b)
            const bool cuA = uo.c[std::size_t(next(j))], cuB = uo.c[std::size_t(prev(j))];
            tris[std::size_t(U1)] = {{d, b, pi}, {T2, U2, uB}, {split, false, cuB}, true};
            tris[std::size_t(U2)] = {{d, pi, a}, {T1, uA, U1}, {split, cuA, false}, true};
            replaceNeighbor(uA, u, U2);
        }
        m_last = T1;
        legalize(T1, 2, touched);
        legalize(T2, 1, touched);
        if (u >= 0) {
            legalize(U1, 2, touched);
            legalize(U2, 1, touched);
        }
        if (touched) {
            touched->insert(touched->end(), {T1, T2});
            if (u >= 0)
                touched->insert(touched->end(), {U1, U2});
        }
        return pi;
    }

    // Flips edge i of t (the diagonal of the quad t ∪ neighbour); t keeps
    // the vertex opposite the edge. Returns the neighbour's index.
    int flip(int t, int i)
    {
        const Tri x = tris[std::size_t(t)];
        const int u = x.n[std::size_t(i)];
        const Tri y = tris[std::size_t(u)];
        const int j = backIndex(u, t);
        const int p = x.v[std::size_t(i)], a = x.v[std::size_t(next(i))], b = x.v[std::size_t(prev(i))];
        const int q = y.v[std::size_t(j)];
        const int nA = x.n[std::size_t(next(i))], nB = x.n[std::size_t(prev(i))]; // across (b,p), (p,a)
        const bool cA = x.c[std::size_t(next(i))], cB = x.c[std::size_t(prev(i))];
        // y = (q, b, a): across (a, q) is opposite b, across (q, b) is opposite a.
        const int uA = y.n[std::size_t(next(j))], uB = y.n[std::size_t(prev(j))];
        const bool cuA = y.c[std::size_t(next(j))], cuB = y.c[std::size_t(prev(j))];
        tris[std::size_t(t)] = {{p, a, q}, {uA, u, nB}, {cuA, false, cB}, true};
        tris[std::size_t(u)] = {{p, q, b}, {uB, nA, t}, {cuB, cA, false}, true};
        replaceNeighbor(uA, u, t);
        replaceNeighbor(nA, t, u);
        return u;
    }

    // Restores the Delaunay property across edge i of t, which is opposite
    // a newly inserted vertex, flipping recursively.
    void legalize(int t, int i, std::vector<int> *touched)
    {
        std::vector<std::pair<int, int>> stack{{t, i}};
        while (!stack.empty()) {
            const auto [tt, ii] = stack.back();
            stack.pop_back();
            const Tri &x = tris[std::size_t(tt)];
            const int u = x.n[std::size_t(ii)];
            if (u < 0 || x.c[std::size_t(ii)])
                continue;
            const int j = backIndex(u, tt);
            const int q = tris[std::size_t(u)].v[std::size_t(j)];
            if (incircle(P(x.v[0]), P(x.v[1]), P(x.v[2]), P(q)) <= 0.0)
                continue;
            const int p = x.v[std::size_t(ii)];
            const int uu = flip(tt, ii);
            if (touched)
                touched->insert(touched->end(), {tt, uu});
            // Both new triangles keep p; check their edges opposite p.
            stack.emplace_back(tt, indexOf(tt, p));
            stack.emplace_back(uu, indexOf(uu, p));
        }
    }

    int indexOf(int t, int v) const
    {
        const Tri &x = tris[std::size_t(t)];
        return x.v[0] == v ? 0 : x.v[1] == v ? 1 : 2;
    }

    // --- constraints -----------------------------------------------------------------

    bool properCross(Vec2 a, Vec2 b, Vec2 p, Vec2 q) const
    {
        const double o1 = orient(a, b, p), o2 = orient(a, b, q);
        const double o3 = orient(p, q, a), o4 = orient(p, q, b);
        return ((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0));
    }

    bool markConstrained(int a, int b)
    {
        bool found = false;
        for (std::size_t t = 0; t < tris.size(); ++t) {
            if (!tris[t].alive)
                continue;
            const int i = edgeIndex(int(t), a, b);
            if (i >= 0) {
                tris[t].c[std::size_t(i)] = true;
                found = true;
            }
        }
        return found;
    }

    // Makes segment (a, b) an edge of the triangulation (Sloan's method).
    void recoverSegment(int a, int b, int depth = 0)
    {
        if (a == b || depth > 8)
            return;
        if (markConstrained(a, b))
            return;
        const Vec2 A = P(a), B = P(b);
        // A vertex lying on the segment splits it in two.
        for (std::size_t v = kSuperVertices; v < pts.size(); ++v) {
            if (int(v) == a || int(v) == b)
                continue;
            const Vec2 V = pts[v];
            if (nearlyZero(orient(A, B, V), A, B) && dot(V - A, B - A) > 0 && dot(V - B, A - B) > 0) {
                recoverSegment(a, int(v), depth + 1);
                recoverSegment(int(v), b, depth + 1);
                return;
            }
        }
        // The edges crossing the segment.
        std::deque<std::pair<int, int>> crossing;
        for (std::size_t t = 0; t < tris.size(); ++t) {
            const Tri &x = tris[t];
            if (!x.alive)
                continue;
            for (int i = 0; i < 3; ++i) {
                const int p = x.v[std::size_t(next(i))], q = x.v[std::size_t(prev(i))];
                if (p < q && properCross(A, B, P(p), P(q)))
                    crossing.emplace_back(p, q);
            }
        }
        std::vector<std::pair<int, int>> created;
        for (std::size_t guard = 0; !crossing.empty() && guard < 100000; ++guard) {
            const auto [p, q] = crossing.front();
            crossing.pop_front();
            int t = -1, i = -1;
            for (std::size_t k = 0; k < tris.size() && t < 0; ++k)
                if (tris[k].alive && (i = edgeIndex(int(k), p, q)) >= 0)
                    t = int(k);
            if (t < 0)
                continue;
            const Tri &x = tris[std::size_t(t)];
            const int u = x.n[std::size_t(i)];
            if (u < 0)
                continue;
            const int r = x.v[std::size_t(i)];
            const int s = tris[std::size_t(u)].v[std::size_t(backIndex(u, t))];
            // Flippable only if the quad r, p, s, q is strictly convex.
            if (!properCross(P(r), P(s), P(p), P(q))) {
                crossing.emplace_back(p, q);
                continue;
            }
            flip(t, i);
            if (properCross(A, B, P(r), P(s)))
                crossing.emplace_back(std::min(r, s), std::max(r, s));
            else
                created.emplace_back(r, s);
        }
        markConstrained(a, b);
        // Bring the new edges back to Delaunay where the constraint allows.
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto &[p, q] : created) {
                if ((p == a && q == b) || (p == b && q == a))
                    continue;
                for (std::size_t k = 0; k < tris.size(); ++k) {
                    const int i = tris[k].alive ? edgeIndex(int(k), p, q) : -1;
                    if (i < 0 || tris[k].c[std::size_t(i)])
                        continue;
                    const int u = tris[k].n[std::size_t(i)];
                    if (u < 0)
                        break;
                    const Tri &x = tris[k];
                    const int s = tris[std::size_t(u)].v[std::size_t(backIndex(u, int(k)))];
                    if (incircle(P(x.v[0]), P(x.v[1]), P(x.v[2]), P(s)) > 0.0
                        && properCross(P(x.v[std::size_t(i)]), P(s), P(p), P(q))) {
                        flip(int(k), i);
                        changed = true;
                    }
                    break;
                }
            }
        }
    }

    // Deletes the triangles outside the outline: everything reachable from
    // the super-triangle without crossing a constrained edge.
    void removeOutside()
    {
        std::deque<int> queue;
        for (std::size_t t = 0; t < tris.size(); ++t) {
            const Tri &x = tris[t];
            if (x.alive && (x.v[0] < kSuperVertices || x.v[1] < kSuperVertices || x.v[2] < kSuperVertices)) {
                tris[t].alive = false;
                queue.push_back(int(t));
            }
        }
        while (!queue.empty()) {
            const int t = queue.front();
            queue.pop_front();
            for (int i = 0; i < 3; ++i) {
                const int u = tris[std::size_t(t)].n[std::size_t(i)];
                if (u >= 0 && tris[std::size_t(u)].alive && !tris[std::size_t(t)].c[std::size_t(i)]) {
                    tris[std::size_t(u)].alive = false;
                    queue.push_back(u);
                }
            }
        }
        for (Tri &x : tris) {
            if (!x.alive)
                continue;
            for (int i = 0; i < 3; ++i)
                if (x.n[std::size_t(i)] >= 0 && !tris[std::size_t(x.n[std::size_t(i)])].alive) {
                    x.n[std::size_t(i)] = -1;
                    x.c[std::size_t(i)] = true; // the outline
                }
        }
        m_last = firstAlive();
    }

    // --- refinement ------------------------------------------------------------------

    double area(const Tri &x) const { return 0.5 * orient(P(x.v[0]), P(x.v[1]), P(x.v[2])); }

    void refine(const RefineOptions &o)
    {
        const double s = std::sin(o.minAngleDegrees * std::numbers::pi / 180.0);
        const double bound2 = 1.0 / (4.0 * s * s); // (R / shortest edge)² above this = too thin
        const double minSeg2 = o.minSegment * o.minSegment;
        std::deque<int> queue;
        for (std::size_t t = 0; t < tris.size(); ++t)
            if (tris[t].alive)
                queue.push_back(int(t));
        std::vector<int> touched;
        while (!queue.empty() && int(pts.size()) < o.maxVertices + kSuperVertices) {
            const int t = queue.front();
            queue.pop_front();
            const Tri x = tris[std::size_t(t)];
            if (!x.alive)
                continue;
            const Vec2 a = P(x.v[0]), b = P(x.v[1]), c = P(x.v[2]);
            const double e0 = length2(b - c), e1 = length2(c - a), e2 = length2(a - b);
            const double shortest = std::min({e0, e1, e2});
            const Vec2 cc = circumcenter(a, b, c);
            const double r2 = length2(cc - a);
            const double ar = area(x);
            const bool tooThin = r2 > bound2 * shortest && shortest > minSeg2;
            const bool tooBig = o.maxArea > 0.0 && ar > o.maxArea;
            if (!tooThin && !tooBig)
                continue;
            if (tooThin && !tooBig && smallAngleBetweenConstraints(x, e0, e1, e2))
                continue; // a sharp corner of the outline: refining it never ends
            touched.clear();
            if (!insertSteiner(t, cc, minSeg2, &touched))
                continue;
            for (int k : touched)
                queue.push_back(k);
            queue.push_back(t);
        }
    }

    bool smallAngleBetweenConstraints(const Tri &x, double e0, double e1, double e2) const
    {
        // The smallest angle is opposite the shortest edge; its two sides are the other edges.
        const int i = e0 <= e1 && e0 <= e2 ? 0 : e1 <= e2 ? 1 : 2;
        return x.c[std::size_t(next(i))] && x.c[std::size_t(prev(i))];
    }

    // Inserts the circumcenter of t, or splits the outline edge it would
    // encroach. Returns false if nothing could be inserted.
    bool insertSteiner(int t, Vec2 cc, double minSeg2, std::vector<int> *touched)
    {
        // Walk towards the circumcenter, stopping at the outline.
        int cur = t;
        for (std::size_t steps = 0; steps < tris.size() * 3 + 64; ++steps) {
            const Tri &x = tris[std::size_t(cur)];
            int exit = -1;
            for (int i = 0; i < 3; ++i) {
                const Vec2 a = P(x.v[std::size_t(next(i))]), b = P(x.v[std::size_t(prev(i))]);
                if (orient(a, b, cc) < 0 && !nearlyZero(orient(a, b, cc), a, b)) {
                    exit = i;
                    break;
                }
            }
            if (exit < 0)
                break; // cc is in (or on) cur
            if (x.c[std::size_t(exit)] || x.n[std::size_t(exit)] < 0)
                return splitSegment(cur, exit, minSeg2, touched);
            cur = x.n[std::size_t(exit)];
        }
        // Encroachment: cc inside the diametral circle of a nearby outline edge.
        for (int k : {cur, tris[std::size_t(cur)].n[0], tris[std::size_t(cur)].n[1], tris[std::size_t(cur)].n[2]}) {
            if (k < 0)
                continue;
            const Tri &x = tris[std::size_t(k)];
            for (int i = 0; i < 3; ++i) {
                if (!x.c[std::size_t(i)])
                    continue;
                const Vec2 a = P(x.v[std::size_t(next(i))]), b = P(x.v[std::size_t(prev(i))]);
                if (dot(a - cc, b - cc) < 0.0)
                    return splitSegment(k, i, minSeg2, touched);
            }
        }
        const int before = int(pts.size());
        const int v = insert(cc, cur, touched);
        return v >= before;
    }

    bool splitSegment(int t, int i, double minSeg2, std::vector<int> *touched)
    {
        const Tri &x = tris[std::size_t(t)];
        const Vec2 a = P(x.v[std::size_t(next(i))]), b = P(x.v[std::size_t(prev(i))]);
        if (length2(b - a) < 4.0 * minSeg2)
            return false;
        splitEdge(t, i, (a + b) * 0.5, touched);
        return true;
    }

    TriMesh exportMesh() const
    {
        TriMesh mesh;
        std::vector<int> remap(pts.size(), -1);
        for (const Tri &x : tris) {
            if (!x.alive)
                continue;
            std::array<int, 3> tri{};
            for (int k = 0; k < 3; ++k) {
                int &r = remap[std::size_t(x.v[std::size_t(k)])];
                if (r < 0) {
                    r = int(mesh.vertices.size());
                    mesh.vertices.push_back(P(x.v[std::size_t(k)]));
                }
                tri[std::size_t(k)] = r;
            }
            mesh.triangles.push_back(tri);
        }
        return mesh;
    }

    int lastTriangle() const { return m_last; }

private:
    double m_scale = 1.0;
    int m_last = 0;
};

} // namespace

double TriMesh::area() const
{
    double a = 0.0;
    for (const auto &t : triangles)
        a += 0.5 * orient(vertices[std::size_t(t[0])], vertices[std::size_t(t[1])], vertices[std::size_t(t[2])]);
    return a;
}

double polygonArea(const std::vector<Vec2> &polygon)
{
    double a = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i)
        a += cross(polygon[i], polygon[(i + 1) % polygon.size()]);
    return std::abs(a) * 0.5;
}

bool pointInPolygon(const std::vector<Vec2> &polygon, Vec2 p)
{
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 a = polygon[i], b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

TriMesh triangulatePolygon(const std::vector<Vec2> &outline, const std::vector<Vec2> &extraPoints,
                           const RefineOptions &options)
{
    if (outline.size() < 3 || polygonArea(outline) < 1e-6)
        return {};
    Cdt cdt(outline);
    std::vector<int> ids;
    ids.reserve(outline.size());
    for (const Vec2 &p : outline)
        ids.push_back(cdt.insert(p, cdt.lastTriangle()));
    for (const Vec2 &p : extraPoints)
        if (pointInPolygon(outline, p))
            cdt.insert(p, cdt.lastTriangle());
    for (std::size_t i = 0; i < ids.size(); ++i)
        cdt.recoverSegment(ids[i], ids[(i + 1) % ids.size()]);
    cdt.removeOutside();
    cdt.refine(options);
    return cdt.exportMesh();
}

} // namespace mothion::puppet
