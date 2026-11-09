#pragma once

#include "core/document/Camera.h"

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

    // As drawn: nothing moved, turned or scaled (the opacity is another matter).
    bool movesNothing() const { return x == 0 && y == 0 && scaleX == 1 && scaleY == 1 && rotation == 0; }
    // The page's points as they are drawn, to where this pose puts them: scaled and turned around `pivot`, then moved.
    Matrix2x3 matrix(double pivotX, double pivotY) const;

    bool operator==(const CelPose &) const = default;
};

struct CelKey
{
    int frame = 0; // 0-based
    CelPose pose;
    Easing easing; // from this key to the next one

    bool operator==(const CelKey &) const = default;
};

// What the animation does to a cel: where its pivot is, and its pose on the frames it has keys for. Before the first
// key and after the last it stays as that key put it; between two it goes along the first one's easing. The position and
// the rotation go straight along the easing, the scale by ratios (a steady scale looks steady), the opacity straight.
// A cel with no key is not moved at all.
class CelMotion
{
public:
    // The point of the page the cel turns and scales around, before it is moved.
    double pivotX = 0, pivotY = 0;

    const std::vector<CelKey> &keys() const { return m_keys; }
    bool isEmpty() const { return m_keys.empty(); }

    const CelKey *keyAt(int frame) const; // nullptr where there is none
    CelPose poseAt(int frame) const;      // as drawn when there is no key

    // A key at `frame`: replaces the one there (keeping its easing unless one is given) or adds one.
    void setKey(int frame, const CelPose &pose);
    void setKey(int frame, const CelPose &pose, const Easing &easing);
    bool setEasing(int frame, const Easing &easing); // false if there is no key at `frame`
    bool removeKey(int frame);                       // false if there is none
    // Moves a key to another frame; false if there is no key at `from`, or one already at `to`.
    bool moveKey(int from, int to);

    // A pose the cel has on one frame that is not a key (yet): what the Transform and Move tools leave on a frame with no
    // key. It shows (poseAt answers it there) but is not part of the motion: it is not saved and not compared, and it
    // goes when another frame is chosen, unless the person makes it a key first.
    void setTemporary(int frame, const CelPose &pose) { m_temporary = std::make_pair(frame, pose); }
    void clearTemporary() { m_temporary.reset(); }
    bool hasTemporaryAt(int frame) const { return m_temporary && m_temporary->first == frame; }
    bool hasTemporary() const { return m_temporary.has_value(); }

    bool operator==(const CelMotion &o) const { return pivotX == o.pivotX && pivotY == o.pivotY && m_keys == o.m_keys; }

private:
    std::vector<CelKey> m_keys; // by frame, one for each
    std::optional<std::pair<int, CelPose>> m_temporary;
};

} // namespace mothion::doc
