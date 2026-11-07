#pragma once

#include <array>
#include <vector>

namespace mothion::doc {

// How a value goes from one keyframe to the next: held until the next one, straight, or along a cubic Bezier
// curve like CSS's cubic-bezier(x1, y1, x2, y2) (the curve of time: x is how far along the segment, y how far the
// value has gone; (0,0) and (1,1) are its ends).
struct Easing
{
    enum class Kind { Hold, Linear, Bezier };

    Kind kind = Kind::Linear;
    double x1 = 0.25, y1 = 0.1, x2 = 0.25, y2 = 1.0; // Bezier only

    static Easing hold() { return {Kind::Hold, 0, 0, 0, 0}; }
    static Easing linear() { return {Kind::Linear, 0, 0, 1, 1}; }
    static Easing bezier(double x1, double y1, double x2, double y2) { return {Kind::Bezier, x1, y1, x2, y2}; }
    static Easing easeIn() { return bezier(0.42, 0.0, 1.0, 1.0); }
    static Easing easeOut() { return bezier(0.0, 0.0, 0.58, 1.0); }
    static Easing easeInOut() { return bezier(0.42, 0.0, 0.58, 1.0); }

    // How far the value has gone (0..1, more or less with a curve that overshoots) when the time is `u` (0..1)
    // along the segment.
    double apply(double u) const;

    bool operator==(const Easing &) const = default;
};

// A 2D transform as the first two rows of a 3x3 matrix: x' = a x + c y + e, y' = b x + d y + f.
struct Matrix2x3
{
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    void map(double x, double y, double &outX, double &outY) const;
    // Undefined (identity) when the matrix is not invertible.
    Matrix2x3 inverted() const;
    // `then` after this one.
    Matrix2x3 then(const Matrix2x3 &next) const;
};

// Where the camera looks, in pixels of the sheet.
struct CameraPose
{
    double centerX = 0, centerY = 0; // the sheet's point at the middle of the picture
    double zoom = 1;                 // picture pixels for a sheet pixel: 1 shows the whole sheet, 2 half of it
    double rotation = 0;             // degrees, clockwise, accumulating (not wrapped: 0 to 360 is a full turn)

    // The pose that shows the whole sheet.
    static CameraPose wholeSheet(int width, int height) { return {width / 2.0, height / 2.0, 1.0, 0.0}; }

    // Sheet to picture, for a picture of `width` x `height` pixels.
    Matrix2x3 sheetToPicture(int width, int height) const;
    // The picture's corners in the sheet (top left, top right, bottom right, bottom left): the camera's frame.
    std::array<std::array<double, 2>, 4> frame(int width, int height) const;

    bool operator==(const CameraPose &) const = default;
};

struct CameraKey
{
    int frame = 0; // 0-based
    CameraPose pose;
    Easing easing; // from this key to the next one

    bool operator==(const CameraKey &) const = default;
};

// The camera of a project: where it looks at each frame, from keyframes. Before the first key and after the last
// it stays where that key put it; between two it goes along the first one's easing. The position moves straight
// along the easing, the zoom by ratios (a steady zoom looks steady: from 1 to 4 it is 2 halfway) and the
// rotation straight in degrees. An invisible camera is not applied (the picture is the whole sheet).
class Camera
{
public:
    bool visible = true;

    // A camera showing the whole sheet from frame 0 on.
    static Camera wholeSheet(int width, int height);

    const std::vector<CameraKey> &keys() const { return m_keys; }
    bool isEmpty() const { return m_keys.empty(); }

    const CameraKey *keyAt(int frame) const; // nullptr where there is none
    CameraPose poseAt(int frame) const;      // the first pose of an empty camera

    // A key at `frame`: replaces the one there (keeping its easing unless one is given) or adds one.
    void setKey(int frame, const CameraPose &pose);
    void setKey(int frame, const CameraPose &pose, const Easing &easing);
    bool setEasing(int frame, const Easing &easing); // false if there is no key at `frame`
    bool removeKey(int frame);                       // false if there is none
    // Moves a key to another frame; false if there is no key at `from`, or one already at `to`.
    bool moveKey(int from, int to);

    bool operator==(const Camera &) const = default;

private:
    std::vector<CameraKey> m_keys; // by frame, one for each
};

} // namespace mothion::doc
