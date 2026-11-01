#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mothion::brush {

class Lcg;
struct BrushInput;

// A curve of a dynamics input: x in, y out, both 0..1.
//
// With two points it is a straight line. With more, each inner point P(i) is
// the control point of a quadratic Bézier running from the midpoint of
// P(i-1)P(i) to the midpoint of P(i)P(i+1) (the first and last pieces start and
// end on the end points). The curve passes through the ends and "follows" the
// inner points without touching them, like the usual curve editor.
class EffectorCurve
{
public:
    struct Point
    {
        double x = 0.0;
        double y = 0.0;
    };

    EffectorCurve() = default;
    explicit EffectorCurve(std::vector<Point> points);

    bool isEmpty() const { return m_points.size() < 2; }
    const std::vector<Point> &points() const { return m_points; }
    double map(double x) const;

private:
    std::vector<Point> m_points;
};

// Inputs a parameter can depend on (bit flags, as stored in brush files).
enum EffectorInput : std::uint32_t {
    InputPressure = 0x10,
    InputVelocity = 0x20,
    InputTilt = 0x40,
    InputRandom = 0x80,  // no curve: uniform random per dab
    InputInOut = 0x100,  // no curve: start/end-of-stroke factor
};

// The dynamics of one brush parameter: which inputs change it and how.
//
//   v = base
//   × (pMin + (1 − pMin)·pressureCurve(pressure))           if pressure
//   × (vFrom + (vTo − vFrom)·velocityCurve(velocity))        if velocity (may exceed 1)
//   × (tMin + (1 − tMin)·tiltCurve(tilt))                    if tilt (and the input has it)
//   × (rMin + (1 − rMin)·random())                           if random and allowed
//   × (ioMin + (1 − ioMin)·inOut)                            if in/out
//
// Percentages from files are stored here as fractions.
struct Effector
{
    std::uint32_t available = 0xF0; // inputs offered for this parameter
    std::uint32_t active = 0;        // inputs switched on
    double pressureMin = 0.0;
    double velocityFrom = 1.0;
    double velocityTo = 5.0;
    double tiltMin = 0.0;
    double randomMin = 0.0;
    double inOutMin = 0.0;
    EffectorCurve pressure;
    EffectorCurve velocity;
    EffectorCurve tilt;

    // Effector with just the pressure input, along `curve`, from `minimum` up.
    static Effector pressureOnly(EffectorCurve curve = {}, double minimum = 0.0);

    // Decodes the binary form used by .sut files: twelve big-endian int32
    // (header size, available, active, pressure min %, velocity from %,
    // tilt min %, random min %, -, pressure / velocity curve bytes,
    // velocity to %, tilt curve bytes), then each curve as int32 12, N, 16
    // followed by N big-endian double (x, y). nullopt if malformed.
    static std::optional<Effector> parse(std::span<const std::uint8_t> blob);

    std::uint32_t inputs() const { return active & available; }
    bool uses(EffectorInput input) const { return (inputs() & input) != 0; }

    // An input switched on without a curve maps linearly. `rng` null leaves
    // out the random factor.
    double evaluate(double base, const BrushInput &in, Lcg *rng) const;
};

} // namespace mothion::brush
