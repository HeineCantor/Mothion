#pragma once

#include "core/PenSample.h"

#include <optional>

class QMouseEvent;
class QPointingDevice;
class QTabletEvent;

namespace mothion::input {

// The boundary between Qt input events and the rest of the program.
// Stateless conversion functions: Qt event in, PenSample out.

// Pressure used for the mouse fallback (a mouse has no pressure).
inline constexpr float kMousePressure = 1.0f;

PenSample toPenSample(const QTabletEvent &event);

// Returns nothing for mouse events Qt synthesized from tablet or touch input:
// those duplicate samples we already got as QTabletEvents.
std::optional<PenSample> toPenSample(const QMouseEvent &event);

InputDeviceInfo describeDevice(const QPointingDevice *device);

} // namespace mothion::input
