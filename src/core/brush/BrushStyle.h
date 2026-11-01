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

// How the start and end of a stroke are measured (BrushInOutType).
enum class InOutType : std::uint8_t {
    Length = 0, // fixed lengths, px
    Ratio = 1,  // fractions of the whole stroke
    Fade = 2,   // the end only: fades over `outLength` from the start, live
};

// Start and end of the stroke: near its ends the in/out factor of each
// point (BrushInput::inOut) goes to 0 along a quarter sine, and parameters
// whose Effector uses InputInOut follow it.
struct InOutStyle
{
    bool in = false;
    bool out = false;
    InOutType type = InOutType::Length;
    double inLength = 0.0, outLength = 0.0; // px (Length, Fade)
    double inRatio = 0.0, outRatio = 0.0;   // fraction of the stroke (Ratio)
};

// Which colors the color variations change (BrushChangeColorTarget).
enum class ColorTarget : std::uint8_t { Both = 0, Main = 1, Secondary = 2 };

// Color of the dabs: a blend of the main and secondary colors, each shifted
// in hue/saturation/value. Per-dab values go through their Effectors (a
// random input with a −100% minimum gives a shift either way); per-stroke
// values are drawn once per stroke.
struct ColorStyle
{
    double subColor = 0.0;   // 0..1 towards the secondary color
    Effector subColorEffector;
    double hue = 0.0;        // degrees
    Effector hueEffector;
    double saturation = 0.0; // fraction
    Effector saturationEffector;
    double value = 0.0;      // fraction
    Effector valueEffector;

    bool perStroke = false;
    double strokeSubColor = 0.0, strokeHue = 0.0, strokeSaturation = 0.0, strokeValue = 0.0;

    ColorTarget target = ColorTarget::Main;

    bool varies() const { return subColor != 0.0 || hue != 0.0 || saturation != 0.0 || value != 0.0 || perStroke; }
};

// Watercolor-style mixing with the paint under each dab.
struct MixStyle
{
    bool on = false;
    double amount = 1.0;    // paint amount: share of the brush color in the mix
    Effector amountEffector;
    double density = 1.0;   // paint density: how opaque the dab stays on thin paint
    Effector densityEffector;
    double stretch = 0.0;   // color stretch: share of the picked color kept from dab to dab
};

// Darkened, uneven rim of the stroke, applied when the pen lifts.
struct WaterEdgeStyle
{
    bool on = false;
    double radius = 3.0;      // px
    double alphaPower = 0.2;  // 0..1, alpha added along the rim
    double valuePower = 0.0;  // 0..1, darkening of the rim
    double blur = 0.0;        // px, softens the rim afterwards
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

    bool ribbon = false;        // stretch the tip between dabs instead of stamping

    InOutStyle inOut;
    // Stroke correction level (0 = off): the first max(3, n) points get a
    // quarter-sine pressure ramp when the stroke ends.
    int correction = 0;
    bool flickerFilter = false; // drop small back-and-forth jitter between points
    // Correction after the stroke (0 = off): simplifies the finished stroke
    // into fewer, smoothly joined points.
    int revision = 0;

    ColorStyle color;
    MixStyle mix;
    WaterEdgeStyle waterEdge;
};

// Color variations of one dab, resolved against the tool colors when drawn.
struct DabColor
{
    double subColor = 0.0; // 0..1 towards the secondary color
    double hue = 0.0;      // degrees
    double saturation = 0.0, value = 0.0;
    ColorTarget target = ColorTarget::Main;

    bool isPlain() const { return subColor == 0.0 && hue == 0.0 && saturation == 0.0 && value == 0.0; }
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
    DabColor color;
    double mixAmount = 1.0;  // MixStyle values at this dab
    double mixDensity = 1.0;
};

} // namespace mothion::brush
