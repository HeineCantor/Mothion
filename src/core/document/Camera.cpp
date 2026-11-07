#include "core/document/Camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::doc {

namespace {

// x(t) of the Bezier whose control points are (0,0), (x1,.), (x2,.), (1,1).
double bezierAt(double t, double p1, double p2)
{
    const double s = 1.0 - t;
    return 3.0 * s * s * t * p1 + 3.0 * s * t * t * p2 + t * t * t;
}

double bezierSlope(double t, double p1, double p2)
{
    const double s = 1.0 - t;
    return 3.0 * s * s * p1 + 6.0 * s * t * (p2 - p1) + 3.0 * t * t * (1.0 - p2);
}

} // namespace

double Easing::apply(double u) const
{
    switch (kind) {
    case Kind::Hold: return u >= 1.0 ? 1.0 : 0.0;
    case Kind::Linear: return u;
    case Kind::Bezier: break;
    }
    if (u <= 0.0)
        return 0.0;
    if (u >= 1.0)
        return 1.0;
    // The curve's parameter t for which x(t) = u: Newton's steps, then bisection when the slope is too flat.
    const double cx1 = std::clamp(x1, 0.0, 1.0), cx2 = std::clamp(x2, 0.0, 1.0);
    double t = u;
    for (int i = 0; i < 8; ++i) {
        const double error = bezierAt(t, cx1, cx2) - u;
        if (std::abs(error) < 1e-9)
            return bezierAt(t, y1, y2);
        const double slope = bezierSlope(t, cx1, cx2);
        if (std::abs(slope) < 1e-6)
            break;
        t -= error / slope;
    }
    double low = 0.0, high = 1.0;
    t = u;
    for (int i = 0; i < 60; ++i) {
        const double x = bezierAt(t, cx1, cx2);
        if (std::abs(x - u) < 1e-9)
            break;
        (x < u ? low : high) = t;
        t = (low + high) / 2.0;
    }
    return bezierAt(t, y1, y2);
}

void Matrix2x3::map(double x, double y, double &outX, double &outY) const
{
    outX = a * x + c * y + e;
    outY = b * x + d * y + f;
}

Matrix2x3 Matrix2x3::inverted() const
{
    const double det = a * d - b * c;
    if (std::abs(det) < 1e-300)
        return {};
    Matrix2x3 r;
    r.a = d / det;
    r.b = -b / det;
    r.c = -c / det;
    r.d = a / det;
    r.e = -(r.a * e + r.c * f);
    r.f = -(r.b * e + r.d * f);
    return r;
}

Matrix2x3 Matrix2x3::then(const Matrix2x3 &n) const
{
    Matrix2x3 r;
    r.a = n.a * a + n.c * b;
    r.b = n.b * a + n.d * b;
    r.c = n.a * c + n.c * d;
    r.d = n.b * c + n.d * d;
    r.e = n.a * e + n.c * f + n.e;
    r.f = n.b * e + n.d * f + n.f;
    return r;
}

Matrix2x3 CameraPose::sheetToPicture(int width, int height) const
{
    // The centre to the origin, the camera's turn undone (the picture turns the other way), the zoom, then the
    // origin to the middle of the picture.
    const double radians = -rotation * std::numbers::pi / 180.0;
    const double cs = std::cos(radians) * zoom, sn = std::sin(radians) * zoom;
    Matrix2x3 turn;
    turn.a = cs;
    turn.b = sn;
    turn.c = -sn;
    turn.d = cs;
    const Matrix2x3 toOrigin{1, 0, 0, 1, -centerX, -centerY};
    const Matrix2x3 toMiddle{1, 0, 0, 1, width / 2.0, height / 2.0};
    return toOrigin.then(turn).then(toMiddle);
}

std::array<std::array<double, 2>, 4> CameraPose::frame(int width, int height) const
{
    const Matrix2x3 back = sheetToPicture(width, height).inverted();
    std::array<std::array<double, 2>, 4> corners{};
    const double xs[4] = {0.0, double(width), double(width), 0.0};
    const double ys[4] = {0.0, 0.0, double(height), double(height)};
    for (int i = 0; i < 4; ++i)
        back.map(xs[i], ys[i], corners[std::size_t(i)][0], corners[std::size_t(i)][1]);
    return corners;
}

Camera Camera::wholeSheet(int width, int height)
{
    Camera camera;
    camera.setKey(0, CameraPose::wholeSheet(width, height));
    return camera;
}

const CameraKey *Camera::keyAt(int frame) const
{
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame,
                                     [](const CameraKey &k, int f) { return k.frame < f; });
    return it != m_keys.end() && it->frame == frame ? &*it : nullptr;
}

CameraPose Camera::poseAt(int frame) const
{
    if (m_keys.empty())
        return {};
    // The first key after `frame`.
    const auto next = std::upper_bound(m_keys.begin(), m_keys.end(), frame,
                                       [](int f, const CameraKey &k) { return f < k.frame; });
    if (next == m_keys.begin())
        return next->pose; // before the first key
    const CameraKey &from = *(next - 1);
    if (next == m_keys.end())
        return from.pose; // after the last one
    const double u = double(frame - from.frame) / double(next->frame - from.frame);
    const double k = from.easing.apply(u);
    const CameraPose &a = from.pose, &b = next->pose;
    CameraPose pose;
    pose.centerX = a.centerX + (b.centerX - a.centerX) * k;
    pose.centerY = a.centerY + (b.centerY - a.centerY) * k;
    pose.rotation = a.rotation + (b.rotation - a.rotation) * k;
    // By ratios: a steady zoom has the same ratio at each step.
    pose.zoom = a.zoom > 0 && b.zoom > 0 ? a.zoom * std::pow(b.zoom / a.zoom, k) : a.zoom + (b.zoom - a.zoom) * k;
    return pose;
}

void Camera::setKey(int frame, const CameraPose &pose)
{
    const CameraKey *existing = keyAt(frame);
    setKey(frame, pose, existing ? existing->easing : Easing::easeInOut());
}

void Camera::setKey(int frame, const CameraPose &pose, const Easing &easing)
{
    frame = std::max(0, frame);
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame,
                                     [](const CameraKey &k, int f) { return k.frame < f; });
    if (it != m_keys.end() && it->frame == frame) {
        it->pose = pose;
        it->easing = easing;
        return;
    }
    m_keys.insert(it, CameraKey{frame, pose, easing});
}

bool Camera::setEasing(int frame, const Easing &easing)
{
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame,
                                     [](const CameraKey &k, int f) { return k.frame < f; });
    if (it == m_keys.end() || it->frame != frame)
        return false;
    it->easing = easing;
    return true;
}

bool Camera::removeKey(int frame)
{
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame,
                                     [](const CameraKey &k, int f) { return k.frame < f; });
    if (it == m_keys.end() || it->frame != frame)
        return false;
    m_keys.erase(it);
    return true;
}

bool Camera::moveKey(int from, int to)
{
    to = std::max(0, to);
    const CameraKey *key = keyAt(from);
    if (!key || (from != to && keyAt(to)))
        return false;
    if (from == to)
        return true;
    CameraKey moved = *key;
    moved.frame = to;
    removeKey(from);
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), to,
                                     [](const CameraKey &k, int f) { return k.frame < f; });
    m_keys.insert(it, moved);
    return true;
}

} // namespace mothion::doc
