#include "core/document/CelMotion.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::doc {

Matrix2x3 CelPose::matrix(double pivotX, double pivotY) const
{
    const double radians = rotation * std::numbers::pi / 180.0;
    const double cs = std::cos(radians), sn = std::sin(radians);
    // The pivot to the origin, scaled, turned (clockwise on a page whose y goes down), then back to the pivot and moved.
    Matrix2x3 scale;
    scale.a = scaleX;
    scale.d = scaleY;
    Matrix2x3 turn;
    turn.a = cs;
    turn.b = sn;
    turn.c = -sn;
    turn.d = cs;
    return Matrix2x3{1, 0, 0, 1, -pivotX, -pivotY}.then(scale).then(turn).then(Matrix2x3{1, 0, 0, 1, pivotX + x, pivotY + y});
}

const CelKey *CelMotion::keyAt(int frame) const
{
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame,
                                     [](const CelKey &k, int f) { return k.frame < f; });
    return it != m_keys.end() && it->frame == frame ? &*it : nullptr;
}

CelPose CelMotion::poseAt(int frame) const
{
    if (m_temporary && m_temporary->first == frame)
        return m_temporary->second;
    if (m_keys.empty())
        return {};
    const auto next = std::upper_bound(m_keys.begin(), m_keys.end(), frame,
                                       [](int f, const CelKey &k) { return f < k.frame; });
    if (next == m_keys.begin())
        return next->pose; // before the first key
    const CelKey &from = *(next - 1);
    if (next == m_keys.end())
        return from.pose; // after the last one
    const double u = double(frame - from.frame) / double(next->frame - from.frame);
    const double k = from.easing.apply(u);
    const CelPose &a = from.pose, &b = next->pose;
    auto straight = [k](double p, double q) { return p + (q - p) * k; };
    // By ratios: a steady scale has the same ratio at each step (and straight when one of them is not positive).
    auto ratio = [k](double p, double q) { return p > 0 && q > 0 ? p * std::pow(q / p, k) : p + (q - p) * k; };
    CelPose pose;
    pose.x = straight(a.x, b.x);
    pose.y = straight(a.y, b.y);
    pose.rotation = straight(a.rotation, b.rotation);
    pose.scaleX = ratio(a.scaleX, b.scaleX);
    pose.scaleY = ratio(a.scaleY, b.scaleY);
    pose.opacity = std::clamp(straight(a.opacity, b.opacity), 0.0, 1.0);
    return pose;
}

void CelMotion::setKey(int frame, const CelPose &pose)
{
    const CelKey *existing = keyAt(frame);
    setKey(frame, pose, existing ? existing->easing : Easing::easeInOut());
}

void CelMotion::setKey(int frame, const CelPose &pose, const Easing &easing)
{
    frame = std::max(0, frame);
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame,
                                     [](const CelKey &k, int f) { return k.frame < f; });
    if (it != m_keys.end() && it->frame == frame) {
        it->pose = pose;
        it->easing = easing;
        return;
    }
    m_keys.insert(it, CelKey{frame, pose, easing});
}

bool CelMotion::setEasing(int frame, const Easing &easing)
{
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame,
                                     [](const CelKey &k, int f) { return k.frame < f; });
    if (it == m_keys.end() || it->frame != frame)
        return false;
    it->easing = easing;
    return true;
}

bool CelMotion::removeKey(int frame)
{
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame,
                                     [](const CelKey &k, int f) { return k.frame < f; });
    if (it == m_keys.end() || it->frame != frame)
        return false;
    m_keys.erase(it);
    return true;
}

bool CelMotion::moveKey(int from, int to)
{
    to = std::max(0, to);
    const CelKey *key = keyAt(from);
    if (!key || (from != to && keyAt(to)))
        return false;
    if (from == to)
        return true;
    CelKey moved = *key;
    moved.frame = to;
    removeKey(from);
    const auto it = std::lower_bound(m_keys.begin(), m_keys.end(), to,
                                     [](const CelKey &k, int f) { return k.frame < f; });
    m_keys.insert(it, moved);
    return true;
}

} // namespace mothion::doc
