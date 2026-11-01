#pragma once

#include "core/brush/Effector.h"

#include <cstdint>

namespace mothion::brush {

// What the dynamics of a dab can depend on, at one point of the stroke.
struct BrushInput
{
    double x = 0.0, y = 0.0;  // document pixels
    double pressure = 1.0;    // 0..1, after the global pressure curve
    double velocity = 0.0;    // 0..1 (see VelocityMeter)
    double tilt = 0.0;        // 0..1, 0 = pen upright
    double penAngle = 0.0;    // degrees: barrel rotation, or tilt direction
    double inOut = 1.0;       // 0..1 start/end-of-stroke factor, 1 in the middle
    double sizeScale = 1.0;   // multiplies the size
    double flowScale = 1.0;   // multiplies the flow
    double direction = 0.0;   // degrees, direction of travel (document, y down)
};

// How dabs are spaced along the stroke (BrushAutoIntervalType).
enum class SpacingMode : std::uint8_t {
    Custom = 0,   // `spacing` × diameter, through `spacingEffector`
    Narrow = 1,   // presets that follow hardness and thickness
    Normal = 2,
    Wide = 3,
    Auto = 4,     // from the soft edge width
    AutoCapped = 5, // Auto, but never wider than Custom
};

// Which tip image a dab uses, k = dab index in the stroke, N = tips.
enum class TipOrder : std::uint8_t {
    Cycle = 0,     // k mod N
    PingPong = 1,  // 0 1 2 .. N-1 .. 2 1 0 1 ..
    HoldLast = 2,  // min(k, N-1)
    Random = 3,    // uniform
    Once = 4,      // k while k < N, then no dab
};

// Tip flip flags (BrushPatternReverseHorizontal | ...Vertical << 4).
enum TipFlip : std::uint32_t {
    FlipHAlways = 0x1, FlipHRandom = 0x2, FlipHAlternate = 0x4,
    FlipVAlways = 0x10, FlipVRandom = 0x20, FlipVAlternate = 0x40,
};

// Rotation mode flags (BrushRotationEffector).
enum RotationMode : std::uint32_t {
    RotationOn = 0x1,
    RotationPenAngle = 0x30,
    RotationDirection = 0x40,
    RotationRandom = 0x80,
};

// A brush, "compiled": every value the dab computation needs, in engine
// units (pixels, fractions, degrees). Built from presets or from imported
// files. The canvas fills `size` from the tool settings; the stroke
// opacity is applied when composing, outside the engine.
struct BrushStyle
{
    double size = 6.0;          // base radius, px
    Effector sizeEffector = Effector::pressureOnly();
    bool atLeastOnePixel = false;

    double dabOpacity = 1.0;    // per dab, multiplies the flow
    Effector dabOpacityEffector;
    double flow = 1.0;          // per-dab alpha, builds up within the stroke
    Effector flowEffector;
    bool adjustFlowBySpacing = false; // dense dabs get less flow

    double hardness = 1.0;      // 0..1; < 1 = soft round dab

    SpacingMode spacingMode = SpacingMode::Custom;
    double spacing = 0.075;     // fraction of the diameter (Custom)
    Effector spacingEffector;

    double thickness = 1.0;     // ellipse ratio, 1 = round
    Effector thicknessEffector;
    bool verticalThickness = false;

    double rotation = 0.0;      // degrees
    std::uint32_t rotationMode = 0;
    double rotationRandom = 0.0; // fraction of 360° for RotationRandom

    int tipCount = 0;           // 0 = procedural round dab
    TipOrder tipOrder = TipOrder::Cycle;
    std::uint32_t tipFlip = 0;

    bool spray = false;
    double spraySize = 1.0;     // spray radius: px, or fraction of the size if synced
    bool spraySizeSynced = true;
    Effector spraySizeEffector;
    double sprayDensity = 1.0;  // particles per dab position
    Effector sprayDensityEffector;
    double sprayBias = 0.0;     // −1 edge .. 0 even .. 1 centre
};

// One stamp, ready to rasterize. The footprint is an ellipse (or the tip
// image) of half-axes `radius` across and `radius × thickness` along y,
// rotated by `angle`, optionally mirrored.
struct BrushDab
{
    double x = 0.0, y = 0.0; // document pixels
    double radius = 1.0;     // includes the soft-edge extension
    double thickness = 1.0;
    double angle = 0.0;      // degrees
    double alpha = 1.0;      // flow × dab opacity, 0..1
    int tip = -1;            // tip image index, −1 = round
    bool mirror = false;
};

} // namespace mothion::brush
