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

CelPose channelOf(Channel channel, const CelPose &pose)
{
    CelPose only;
    copyChannel(channel, pose, only);
    return only;
}

void copyChannel(Channel channel, const CelPose &from, CelPose &into)
{
    switch (channel) {
    case Channel::Position: into.x = from.x; into.y = from.y; break;
    case Channel::Rotation: into.rotation = from.rotation; break;
    case Channel::Scale: into.scaleX = from.scaleX; into.scaleY = from.scaleY; break;
    case Channel::Opacity: into.opacity = from.opacity; break;
    }
}

bool channelsDiffer(Channel channel, const CelPose &a, const CelPose &b)
{
    return channelOf(channel, a) != channelOf(channel, b);
}

namespace {
template <class Keys> auto keyAtOrAfter(Keys &keys, int frame)
{
    return std::lower_bound(keys.begin(), keys.end(), frame, [](const CelKey &k, int f) { return k.frame < f; });
}
} // namespace

bool CelMotion::isEmpty() const
{
    for (const auto &keys : m_keys)
        if (!keys.empty())
            return false;
    return true;
}

std::vector<int> CelMotion::keyFrames() const
{
    std::vector<int> frames;
    for (const auto &keys : m_keys)
        for (const CelKey &k : keys)
            frames.push_back(k.frame);
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}

const CelKey *CelMotion::keyAt(Channel channel, int frame) const
{
    const auto &keys = m_keys[int(channel)];
    const auto it = keyAtOrAfter(keys, frame);
    return it != keys.end() && it->frame == frame ? &*it : nullptr;
}

bool CelMotion::hasKeyAt(int frame) const
{
    for (Channel c : kChannels)
        if (keyAt(c, frame))
            return true;
    return false;
}

CelPose CelMotion::poseAt(int frame) const
{
    if (m_temporary && m_temporary->first == frame)
        return m_temporary->second;
    return interpolatedAt(frame);
}

CelPose CelMotion::interpolatedAt(int frame) const
{
    CelPose pose;
    for (Channel c : kChannels)
        copyChannel(c, channelPose(c, frame), pose);
    return pose;
}

// The pose a channel's keys give on `frame`; only the fields of the channel mean anything.
CelPose CelMotion::channelPose(Channel channel, int frame) const
{
    const auto &keys = m_keys[int(channel)];
    if (keys.empty())
        return {};
    const auto next = std::upper_bound(keys.begin(), keys.end(), frame, [](int f, const CelKey &k) { return f < k.frame; });
    if (next == keys.begin())
        return next->pose; // before the first key
    const CelKey &from = *(next - 1);
    if (next == keys.end())
        return from.pose; // after the last one
    const double u = double(frame - from.frame) / double(next->frame - from.frame);
    const double k = from.easing.apply(u);
    const CelPose &a = from.pose, &b = next->pose;
    auto straight = [k](double p, double q) { return p + (q - p) * k; };
    // By ratios: a steady scale has the same ratio at each step (and straight when one of them is not positive).
    auto ratio = [k](double p, double q) { return p > 0 && q > 0 ? p * std::pow(q / p, k) : p + (q - p) * k; };
    CelPose pose;
    switch (channel) {
    case Channel::Position:
        pose.x = straight(a.x, b.x);
        pose.y = straight(a.y, b.y);
        break;
    case Channel::Rotation: pose.rotation = straight(a.rotation, b.rotation); break;
    case Channel::Scale:
        pose.scaleX = ratio(a.scaleX, b.scaleX);
        pose.scaleY = ratio(a.scaleY, b.scaleY);
        break;
    case Channel::Opacity: pose.opacity = std::clamp(straight(a.opacity, b.opacity), 0.0, 1.0); break;
    }
    return pose;
}

void CelMotion::setKey(Channel channel, int frame, const CelPose &pose)
{
    const CelKey *existing = keyAt(channel, frame);
    setKey(channel, frame, pose, existing ? existing->easing : Easing::easeInOut());
}

void CelMotion::setKey(Channel channel, int frame, const CelPose &pose, const Easing &easing)
{
    frame = std::max(0, frame);
    auto &keys = m_keys[int(channel)];
    const auto it = keyAtOrAfter(keys, frame);
    const CelPose own = channelOf(channel, pose);
    if (it != keys.end() && it->frame == frame) {
        it->pose = own;
        it->easing = easing;
        return;
    }
    keys.insert(it, CelKey{frame, own, easing});
}

bool CelMotion::setEasing(Channel channel, int frame, const Easing &easing)
{
    auto &keys = m_keys[int(channel)];
    const auto it = keyAtOrAfter(keys, frame);
    if (it == keys.end() || it->frame != frame)
        return false;
    it->easing = easing;
    return true;
}

bool CelMotion::removeKey(Channel channel, int frame)
{
    auto &keys = m_keys[int(channel)];
    const auto it = keyAtOrAfter(keys, frame);
    if (it == keys.end() || it->frame != frame)
        return false;
    keys.erase(it);
    return true;
}

bool CelMotion::moveKey(Channel channel, int from, int to)
{
    to = std::max(0, to);
    const CelKey *key = keyAt(channel, from);
    if (!key || (from != to && keyAt(channel, to)))
        return false;
    if (from == to)
        return true;
    CelKey moved = *key;
    moved.frame = to;
    removeKey(channel, from);
    auto &keys = m_keys[int(channel)];
    keys.insert(keyAtOrAfter(keys, to), moved);
    return true;
}

bool CelMotion::setEasingAll(int frame, const Easing &easing)
{
    bool any = false;
    for (Channel c : kChannels)
        any |= setEasing(c, frame, easing);
    return any;
}

bool CelMotion::removeKeysAt(int frame)
{
    bool any = false;
    for (Channel c : kChannels)
        any |= removeKey(c, frame);
    return any;
}

bool CelMotion::moveKeysAt(int from, int to)
{
    to = std::max(0, to);
    bool any = false;
    for (Channel c : kChannels) {
        if (!keyAt(c, from))
            continue;
        any = true;
        if (from != to && keyAt(c, to))
            return false; // a place taken in one of them: none moves
    }
    if (!any)
        return false;
    for (Channel c : kChannels)
        moveKey(c, from, to);
    return true;
}

} // namespace mothion::doc
