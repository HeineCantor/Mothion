#pragma once

// Qt-independent description of pointer input.
//
// The Qt adapter (src/input) converts QTabletEvent / QMouseEvent into these
// types; everything downstream (smoothing, brushes, debug overlay) only ever
// sees PenSample. Porting to another toolkit or replaying recorded input
// means producing PenSamples, nothing else.

#include <cstdint>
#include <string>

namespace mothion {

// The physical thing that produced the sample. Not the selected tool:
// flipping a pen to its eraser end gives InputTool::Eraser whatever the palette says.
enum class InputTool : std::uint8_t { Pen, Eraser, Mouse };

// Bit flags for PenSample::buttons. Mapped from Qt's buttons as-is
// (left = tip / left mouse, middle, right); which barrel button maps to
// middle or right differs between drivers and platforms.
enum PenButton : std::uint8_t {
    PenButtonTip = 1u << 0,
    PenButtonMiddle = 1u << 1,
    PenButtonRight = 1u << 2,
};

enum class SamplePhase : std::uint8_t {
    Hover,   // no button down (pen in the air, mouse moving)
    Press,   // first button went down
    Move,    // moved with at least one button down
    Release, // last button went up
};

struct PenSample
{
    double t = 0.0;                    // seconds, platform event timestamp (ms resolution)
    double x = 0.0, y = 0.0;           // widget coordinates, sub-pixel
    float pressure = 0.0f;             // 0..1, raw (the pressure curve is applied later)
    float tiltX = 0.0f, tiltY = 0.0f;  // degrees, -60..60; 0 if unsupported
    float rotation = 0.0f;             // degrees around the pen axis; 0 if unsupported
    InputTool tool = InputTool::Pen;
    SamplePhase phase = SamplePhase::Hover;
    std::uint8_t buttons = 0;          // PenButton flags

    bool tipDown() const { return (buttons & PenButtonTip) != 0; }
};

// What we know about the device a sample came from (for the debug overlay).
struct InputDeviceInfo
{
    std::string name;          // as reported by the platform
    std::string type;          // "Stylus", "Mouse", "Airbrush", ...
    InputTool tool = InputTool::Mouse;
    bool hasPressure = false;
    bool hasTilt = false;
    bool hasRotation = false;
};

struct ProximityEvent
{
    bool entering = false; // true: pen came into range, false: left it
    InputTool tool = InputTool::Pen;
};

} // namespace mothion
