#include "input/TabletInputAdapter.h"

#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>

namespace mothion::input {

namespace {

InputTool toolFor(const QPointingDevice *device)
{
    if (!device || device->type() == QInputDevice::DeviceType::Mouse
        || device->type() == QInputDevice::DeviceType::TouchPad)
        return InputTool::Mouse;
    // Eraser end of a stylus. On X11 this relies on the driver exposing a
    // separate "eraser" device (Qt matches on the device name).
    return device->pointerType() == QPointingDevice::PointerType::Eraser ? InputTool::Eraser
                                                                         : InputTool::Pen;
}

std::uint8_t toButtons(Qt::MouseButtons buttons)
{
    std::uint8_t b = 0;
    if (buttons & Qt::LeftButton)
        b |= PenButtonTip;
    if (buttons & Qt::MiddleButton)
        b |= PenButtonMiddle;
    if (buttons & Qt::RightButton)
        b |= PenButtonRight;
    return b;
}

// Derives the phase from the event type and the buttons held *after* it,
// so the adapter needs no state of its own.
SamplePhase toPhase(QEvent::Type type, Qt::MouseButton changed, Qt::MouseButtons held)
{
    switch (type) {
    case QEvent::TabletPress:
    case QEvent::MouseButtonPress:
        return (held & ~changed) == Qt::NoButton ? SamplePhase::Press : SamplePhase::Move;
    case QEvent::TabletRelease:
    case QEvent::MouseButtonRelease:
        return held == Qt::NoButton ? SamplePhase::Release : SamplePhase::Move;
    default:
        return held == Qt::NoButton ? SamplePhase::Hover : SamplePhase::Move;
    }
}

} // namespace

PenSample toPenSample(const QTabletEvent &event)
{
    PenSample s;
    s.t = double(event.timestamp()) / 1000.0;
    s.x = event.position().x();
    s.y = event.position().y();
    s.pressure = float(event.pressure());
    s.tiltX = float(event.xTilt());
    s.tiltY = float(event.yTilt());
    s.rotation = float(event.rotation());
    s.tool = toolFor(event.pointingDevice());
    s.buttons = toButtons(event.buttons());
    s.phase = toPhase(event.type(), event.button(), event.buttons());
    return s;
}

std::optional<PenSample> toPenSample(const QMouseEvent &event)
{
    const QPointingDevice *device = event.pointingDevice();
    if (device && device->type() != QInputDevice::DeviceType::Mouse
        && device->type() != QInputDevice::DeviceType::TouchPad)
        return std::nullopt;

    PenSample s;
    s.t = double(event.timestamp()) / 1000.0;
    s.x = event.position().x();
    s.y = event.position().y();
    s.tool = InputTool::Mouse;
    s.buttons = toButtons(event.buttons());
    s.phase = toPhase(event.type(), event.button(), event.buttons());
    s.pressure = s.buttons ? kMousePressure : 0.0f;
    return s;
}

InputDeviceInfo describeDevice(const QPointingDevice *device)
{
    InputDeviceInfo info;
    if (!device)
        return info;
    info.name = device->name().toStdString();
    info.tool = toolFor(device);

    switch (device->type()) {
    case QInputDevice::DeviceType::Mouse: info.type = "Mouse"; break;
    case QInputDevice::DeviceType::TouchPad: info.type = "TouchPad"; break;
    case QInputDevice::DeviceType::TouchScreen: info.type = "TouchScreen"; break;
    case QInputDevice::DeviceType::Stylus: info.type = "Stylus"; break;
    case QInputDevice::DeviceType::Airbrush: info.type = "Airbrush"; break;
    case QInputDevice::DeviceType::Puck: info.type = "Puck"; break;
    default: info.type = "Unknown"; break;
    }

    const auto caps = device->capabilities();
    info.hasPressure = caps.testFlag(QInputDevice::Capability::Pressure);
    info.hasTilt = caps.testFlag(QInputDevice::Capability::XTilt);
    info.hasRotation = caps.testFlag(QInputDevice::Capability::Rotation);
    return info;
}

} // namespace mothion::input
