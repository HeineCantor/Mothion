#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// The few 3D types the rig of a model needs, in double: vectors, unit quaternions for turns, and 3×3 / 4×4 matrices
// that take column vectors (p' = M·p), stored column by column as glTF and OpenGL keep them.
namespace mothion::rig3d {

struct Vec3
{
    double x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3 &operator+=(const Vec3 &o) { return *this = *this + o; }
    Vec3 &operator-=(const Vec3 &o) { return *this = *this - o; }
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    bool operator==(const Vec3 &) const = default;
};

inline double dot(const Vec3 &a, const Vec3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(const Vec3 &v)
{
    return std::sqrt(dot(v, v));
}
// `v` made a unit long; the zero vector stays zero.
inline Vec3 normalized(const Vec3 &v)
{
    const double l = length(v);
    return l > 0 ? v / l : Vec3{};
}

// A turn: a unit quaternion w + xi + yj + zk.
struct Quat
{
    double w = 1, x = 0, y = 0, z = 0;

    static Quat axisAngle(const Vec3 &axis, double radians)
    {
        const Vec3 a = rig3d::normalized(axis);
        const double s = std::sin(radians / 2);
        return {std::cos(radians / 2), a.x * s, a.y * s, a.z * s};
    }
    // The shortest turn that takes the direction `from` to `to`.
    static Quat between(const Vec3 &from, const Vec3 &to)
    {
        const Vec3 a = rig3d::normalized(from), b = rig3d::normalized(to);
        const double d = dot(a, b);
        if (d < -1 + 1e-12) { // opposite: half a turn about any axis across them
            Vec3 axis = cross({1, 0, 0}, a);
            if (length(axis) < 1e-6)
                axis = cross({0, 1, 0}, a);
            return axisAngle(axis, 3.14159265358979323846);
        }
        const Vec3 c = cross(a, b);
        return Quat{1 + d, c.x, c.y, c.z}.normalized();
    }

    Quat operator*(const Quat &o) const
    {
        return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
    }
    Quat conjugate() const { return {w, -x, -y, -z}; }
    double norm() const { return std::sqrt(w * w + x * x + y * y + z * z); }
    Quat normalized() const
    {
        const double n = norm();
        return n > 0 ? Quat{w / n, x / n, y / n, z / n} : Quat{};
    }
    Vec3 rotate(const Vec3 &v) const
    {
        const Vec3 u{x, y, z};
        const Vec3 t = cross(u, v) * 2.0;
        return v + t * w + cross(u, t);
    }
    bool operator==(const Quat &) const = default;
};

inline double dot(const Quat &a, const Quat &b)
{
    return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

// 3×3, m[c][r]: column c, row r.
struct Mat3
{
    std::array<std::array<double, 3>, 3> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

    double operator()(int row, int col) const { return m[std::size_t(col)][std::size_t(row)]; }
    double &operator()(int row, int col) { return m[std::size_t(col)][std::size_t(row)]; }
    Vec3 column(int c) const { return {m[std::size_t(c)][0], m[std::size_t(c)][1], m[std::size_t(c)][2]}; }
    static Mat3 fromColumns(const Vec3 &a, const Vec3 &b, const Vec3 &c)
    {
        Mat3 r;
        r.m = {{{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}}};
        return r;
    }
    static Mat3 fromQuat(const Quat &q)
    {
        return fromColumns(q.rotate({1, 0, 0}), q.rotate({0, 1, 0}), q.rotate({0, 0, 1}));
    }
    Vec3 operator*(const Vec3 &v) const { return column(0) * v.x + column(1) * v.y + column(2) * v.z; }
    Mat3 operator*(const Mat3 &o) const { return fromColumns(*this * o.column(0), *this * o.column(1), *this * o.column(2)); }
    Mat3 transposed() const
    {
        Mat3 t;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                t(r, c) = (*this)(c, r);
        return t;
    }
    double determinant() const { return dot(column(0), cross(column(1), column(2))); }
    // The inverse; `ok` false (and the identity) for one that has none.
    Mat3 inverse(bool *ok = nullptr) const
    {
        const Vec3 a = column(0), b = column(1), c = column(2);
        const double det = dot(a, cross(b, c));
        if (ok)
            *ok = std::abs(det) > 1e-300;
        if (std::abs(det) <= 1e-300)
            return {};
        const Vec3 r0 = cross(b, c) / det, r1 = cross(c, a) / det, r2 = cross(a, b) / det; // the rows of the inverse
        return Mat3::fromColumns(r0, r1, r2).transposed();
    }
    // The nearest turn (a matrix with orthonormal columns), by Gram–Schmidt from the first column.
    Mat3 orthonormalized() const
    {
        const Vec3 a = normalized(column(0));
        const Vec3 b = normalized(column(1) - a * dot(a, column(1)));
        return fromColumns(a, b, cross(a, b));
    }
    Quat toQuat() const
    {
        const Mat3 &r = *this;
        const double trace = r(0, 0) + r(1, 1) + r(2, 2);
        Quat q;
        if (trace > 0) {
            const double s = std::sqrt(trace + 1.0) * 2;
            q = {0.25 * s, (r(2, 1) - r(1, 2)) / s, (r(0, 2) - r(2, 0)) / s, (r(1, 0) - r(0, 1)) / s};
        } else if (r(0, 0) > r(1, 1) && r(0, 0) > r(2, 2)) {
            const double s = std::sqrt(1.0 + r(0, 0) - r(1, 1) - r(2, 2)) * 2;
            q = {(r(2, 1) - r(1, 2)) / s, 0.25 * s, (r(0, 1) + r(1, 0)) / s, (r(0, 2) + r(2, 0)) / s};
        } else if (r(1, 1) > r(2, 2)) {
            const double s = std::sqrt(1.0 + r(1, 1) - r(0, 0) - r(2, 2)) * 2;
            q = {(r(0, 2) - r(2, 0)) / s, (r(0, 1) + r(1, 0)) / s, 0.25 * s, (r(1, 2) + r(2, 1)) / s};
        } else {
            const double s = std::sqrt(1.0 + r(2, 2) - r(0, 0) - r(1, 1)) * 2;
            q = {(r(1, 0) - r(0, 1)) / s, (r(0, 2) + r(2, 0)) / s, (r(1, 2) + r(2, 1)) / s, 0.25 * s};
        }
        return q.normalized();
    }
};

// 4×4, m[c * 4 + r]: column c, row r (as glTF stores it).
struct Mat4
{
    std::array<double, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    double operator()(int row, int col) const { return m[std::size_t(col * 4 + row)]; }
    double &operator()(int row, int col) { return m[std::size_t(col * 4 + row)]; }

    static Mat4 translation(const Vec3 &t)
    {
        Mat4 r;
        r(0, 3) = t.x;
        r(1, 3) = t.y;
        r(2, 3) = t.z;
        return r;
    }
    // Translation · rotation · scale, glTF's order for a node.
    static Mat4 trs(const Vec3 &t, const Quat &q, const Vec3 &s)
    {
        const Mat3 r = Mat3::fromQuat(q);
        Mat4 out;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                out(row, c) = r(row, c) * s[c];
        out(0, 3) = t.x;
        out(1, 3) = t.y;
        out(2, 3) = t.z;
        return out;
    }
    Mat4 operator*(const Mat4 &o) const
    {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                double s = 0;
                for (int k = 0; k < 4; ++k)
                    s += (*this)(row, k) * o(k, c);
                r(row, c) = s;
            }
        return r;
    }
    Vec3 point(const Vec3 &p) const // w = 1, with the division by the result's w
    {
        const double x = (*this)(0, 0) * p.x + (*this)(0, 1) * p.y + (*this)(0, 2) * p.z + (*this)(0, 3);
        const double y = (*this)(1, 0) * p.x + (*this)(1, 1) * p.y + (*this)(1, 2) * p.z + (*this)(1, 3);
        const double z = (*this)(2, 0) * p.x + (*this)(2, 1) * p.y + (*this)(2, 2) * p.z + (*this)(2, 3);
        const double w = (*this)(3, 0) * p.x + (*this)(3, 1) * p.y + (*this)(3, 2) * p.z + (*this)(3, 3);
        return w != 0 && w != 1 ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
    }
    Vec3 vector(const Vec3 &v) const // w = 0
    {
        return {(*this)(0, 0) * v.x + (*this)(0, 1) * v.y + (*this)(0, 2) * v.z,
                (*this)(1, 0) * v.x + (*this)(1, 1) * v.y + (*this)(1, 2) * v.z,
                (*this)(2, 0) * v.x + (*this)(2, 1) * v.y + (*this)(2, 2) * v.z};
    }
    Vec3 translationPart() const { return {(*this)(0, 3), (*this)(1, 3), (*this)(2, 3)}; }
    Mat3 linear() const
    {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                r(row, c) = (*this)(row, c);
        return r;
    }
    static Mat4 fromLinear(const Mat3 &l, const Vec3 &t)
    {
        Mat4 r;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                r(row, c) = l(row, c);
        r(0, 3) = t.x;
        r(1, 3) = t.y;
        r(2, 3) = t.z;
        return r;
    }
    // The inverse of an affine matrix (last row 0 0 0 1).
    Mat4 affineInverse() const
    {
        const Mat3 inv = linear().inverse();
        return fromLinear(inv, -(inv * translationPart()));
    }
};

constexpr double kPi = 3.14159265358979323846;

// `a` brought into [-π, π).
inline double wrapAngle(double a)
{
    a = std::fmod(a + kPi, 2 * kPi);
    if (a < 0)
        a += 2 * kPi;
    return a - kPi;
}

} // namespace mothion::rig3d
