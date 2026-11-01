#pragma once

#include <cmath>

namespace mothion::puppet {

struct Vec2
{
    double x = 0.0, y = 0.0;

    friend Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
    friend Vec2 operator*(Vec2 a, double k) { return {a.x * k, a.y * k}; }
    friend bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
};

inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double length2(Vec2 a) { return dot(a, a); }
inline double length(Vec2 a) { return std::sqrt(length2(a)); }

// > 0 if a, b, c turn counter-clockwise (y up; clockwise on screen, y down).
inline double orient(Vec2 a, Vec2 b, Vec2 c) { return cross(b - a, c - a); }

// > 0 if d lies inside the circle through a, b, c (a, b, c with orient > 0).
inline double incircle(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const Vec2 ad = a - d, bd = b - d, cd = c - d;
    return (ad.x * ad.x + ad.y * ad.y) * cross(bd, cd) - (bd.x * bd.x + bd.y * bd.y) * cross(ad, cd)
           + (cd.x * cd.x + cd.y * cd.y) * cross(ad, bd);
}

inline Vec2 circumcenter(Vec2 a, Vec2 b, Vec2 c)
{
    const Vec2 ab = b - a, ac = c - a;
    const double d = 2.0 * cross(ab, ac);
    const double ab2 = length2(ab), ac2 = length2(ac);
    return {a.x + (ac.y * ab2 - ab.y * ac2) / d, a.y + (ab.x * ac2 - ac.x * ab2) / d};
}

// True if segments p1p2 and q1q2 cross in their interiors or touch.
inline bool segmentsIntersect(Vec2 p1, Vec2 p2, Vec2 q1, Vec2 q2)
{
    const double d1 = orient(q1, q2, p1), d2 = orient(q1, q2, p2);
    const double d3 = orient(p1, p2, q1), d4 = orient(p1, p2, q2);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        return true;
    auto onSegment = [](Vec2 a, Vec2 b, Vec2 p) {
        return std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y
               && p.y <= std::max(a.y, b.y);
    };
    return (d1 == 0 && onSegment(q1, q2, p1)) || (d2 == 0 && onSegment(q1, q2, p2))
           || (d3 == 0 && onSegment(p1, p2, q1)) || (d4 == 0 && onSegment(p1, p2, q2));
}

} // namespace mothion::puppet
