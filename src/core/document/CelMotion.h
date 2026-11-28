#pragma once

#include "core/document/Camera.h"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace mothion::doc {

// Where a cel is, on a frame: the whole of what it shows (its drawing's layers together) moved, turned and scaled as
// one rigid piece around the cel's pivot, and as see-through as the opacity says. The cel's own drawings are not
// touched: a pose is what the animation does to them.
struct CelPose
{
    double x = 0, y = 0;                // how far it is moved, in pixels of the page
    double scaleX = 1, scaleY = 1;      // 1 is as drawn; negative turns it over
    double rotation = 0;                // degrees, clockwise, accumulating (not wrapped: 0 to 360 is a full turn)
    double opacity = 1;                 // 0..1, on top of the cel's own opacity
    // A deformation cel's pose (see Deformer): its numbers, empty for its rest. Nothing for any other cel.
    std::vector<double> deform;

    // As drawn: nothing moved, turned or scaled (the opacity is another matter).
    bool movesNothing() const { return x == 0 && y == 0 && scaleX == 1 && scaleY == 1 && rotation == 0; }
    // The page's points as they are drawn, to where this pose puts them: scaled and turned around `pivot`, then moved.
    Matrix2x3 matrix(double pivotX, double pivotY) const;

    bool operator==(const CelPose &) const = default;
};

// The parts of a pose that are keyed on their own: each has its own keys, frames and curves.
enum class Channel
{
    Position, // x, y
    Rotation,
    Scale,    // scaleX, scaleY
    Opacity,
    Deform    // deform: a deformation cel's pose, all its numbers keyed together
};
constexpr int kChannelCount = 5;
constexpr Channel kChannels[kChannelCount] = {Channel::Position, Channel::Rotation, Channel::Scale, Channel::Opacity,
                                              Channel::Deform};

// Only the part of `pose` that belongs to the channel (the rest as a pose that moves nothing has it), or the part copied
// from `from` into `into`.
CelPose channelOf(Channel channel, const CelPose &pose);
void copyChannel(Channel channel, const CelPose &from, CelPose &into);
bool channelsDiffer(Channel channel, const CelPose &a, const CelPose &b);

struct CelKey
{
    int frame = 0; // 0-based
    CelPose pose;  // only the fields of the key's channel mean anything
    Easing easing; // from this key to the next one of its channel

    bool operator==(const CelKey &) const = default;
};

// What the animation does to a cel: where its pivot is, and, for each channel (position, rotation, scale, opacity) its
// own keys. Before the first key of a channel and after the last it stays as that key put it; between two it goes along
// the first one's easing. The position and the rotation go straight along the easing, the scale by ratios (a steady scale
// looks steady), the opacity straight, each number of a deformation's pose straight (one that only one of the two keys
// has stays as that one has it). A channel with no key is not changed at all; a cel with no key in any is not moved.
class CelMotion
{
public:
    // The point of the page the cel turns and scales around, before it is moved.
    double pivotX = 0, pivotY = 0;

    const std::vector<CelKey> &keys(Channel channel) const { return m_keys[int(channel)]; }
    bool isEmpty() const;
    bool hasKeys(Channel channel) const { return !m_keys[int(channel)].empty(); }
    // The frames with a key in any channel, ascending.
    std::vector<int> keyFrames() const;

    const CelKey *keyAt(Channel channel, int frame) const; // nullptr where there is none
    bool hasKeyAt(int frame) const;                          // a key of any channel
    CelPose poseAt(int frame) const;                         // the temporary pose there, or what the keys say
    CelPose interpolatedAt(int frame) const;                 // what the keys say (as drawn for a channel with none)

    // A key at `frame` of a channel, from the part of `pose` that is its: replaces the one there (keeping its easing unless
    // one is given) or adds one.
    void setKey(Channel channel, int frame, const CelPose &pose);
    void setKey(Channel channel, int frame, const CelPose &pose, const Easing &easing);
    bool setEasing(Channel channel, int frame, const Easing &easing); // false if there is no key at `frame`
    bool removeKey(Channel channel, int frame);                       // false if there is none
    // Moves a key to another frame; false if there is no key at `from`, or one already at `to`.
    bool moveKey(Channel channel, int from, int to);
    // The same for every channel that has a key at the frame (false if none has; a move is refused if one of them
    // has a key at `to` already).
    bool setEasingAll(int frame, const Easing &easing);
    bool removeKeysAt(int frame);
    bool moveKeysAt(int from, int to);
    // The same motion played backwards over the frames it spans: a key at frame f goes to first + last − f (first and
    // last of all the channels together, so they stay in step), and each curve is turned around (an ease in becomes an
    // ease out), so that the way there is the way back. The pivot stays.
    CelMotion reversed() const;

    // The motion with the way from the key on `from` to the one on `to` drawn as a spacing chart says: for each of
    // `channels` that has a key on both, one key for each share (how far from the first key's pose to the second's: 0 to
    // 1, and past them for an overshoot or an anticipation), in the order of the shares, on the frames right after
    // `from`, in place of the keys there were between the two. Each of them, and the one on `from`, holds. The key on `to`
    // comes right after the last of them (see spacingEnd()), nearer or further than it was, and every key from there on
    // (of any channel) goes with it.
    CelMotion spaced(const std::vector<Channel> &channels, int from, int to, std::vector<double> shares) const;
    // The last frame between `from` and `to` that a channel not among `channels` has a key on (`from` if none does).
    int spacingBlock(const std::vector<Channel> &channels, int from, int to) const;
    // Where the key that was on `to` goes with `count` keys after `from`: the frame after the last of them, but never
    // back over `block` (see spacingBlock()), whose keys stay where they are.
    static int spacingEnd(int from, int count, int block) { return std::max(from + count, block) + 1; }

    // A value of a channel the cel has on one frame that is not a key (yet): what the Transform and Move tools leave on a
    // frame with no key of the channel. It shows (poseAt answers it there, over what the keys say) but is not part of the
    // motion: it is not saved and not compared, and it goes when another frame is chosen, unless the person makes it a key
    // first. Only one frame has them.
    void setTemporary(Channel channel, int frame, const CelPose &pose);
    void clearTemporary() { m_temporary.reset(); }
    bool hasTemporaryAt(int frame) const { return m_temporary && m_temporary->frame == frame; }
    bool hasTemporary() const { return m_temporary.has_value(); }

    bool operator==(const CelMotion &o) const { return pivotX == o.pivotX && pivotY == o.pivotY && m_keys == o.m_keys; }

private:
    CelPose channelPose(Channel channel, int frame) const;
    std::array<std::vector<CelKey>, kChannelCount> m_keys; // by channel, by frame, one for each
    struct Temporary
    {
        int frame = 0;
        CelPose pose;
        std::array<bool, kChannelCount> channels{}; // the ones that are set
    };
    std::optional<Temporary> m_temporary;
};

} // namespace mothion::doc
