#pragma once

#include <array>

namespace mothion {

// Maps raw pen pressure (0..1) to effective pressure (0..1).
//
// The curve is a cubic Bézier from (0,0) to (1,1) shaped by two control
// points, the same model as a CSS easing curve. Control points are clamped
// to the unit square, which keeps the curve a function of x (monotonic in x).
// The curve is baked into a lookup table, so map() is cheap enough to call
// for every input sample.
class PressureCurve
{
public:
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
        bool operator==(const Point &) const = default;
    };

    PressureCurve(); // linear: output == input
    PressureCurve(Point control1, Point control2);

    Point control1() const { return m_c1; }
    Point control2() const { return m_c2; }

    // Raw pressure in, effective pressure out. Input is clamped to [0,1].
    float map(float pressure) const;

    bool operator==(const PressureCurve &other) const
    {
        return m_c1 == other.m_c1 && m_c2 == other.m_c2;
    }

private:
    static constexpr int kLutSize = 256;

    void rebuildLut();

    Point m_c1;
    Point m_c2;
    std::array<float, kLutSize> m_lut{};
};

} // namespace mothion
