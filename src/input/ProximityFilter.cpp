#include "input/ProximityFilter.h"

#include "input/TabletInputAdapter.h"

#include <QCoreApplication>
#include <QTabletEvent>

namespace mothion::input {

ProximityFilter::ProximityFilter(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installEventFilter(this);
}

bool ProximityFilter::eventFilter(QObject *watched, QEvent *event)
{
    const auto type = event->type();
    if (watched == QCoreApplication::instance()
        && (type == QEvent::TabletEnterProximity || type == QEvent::TabletLeaveProximity)) {
        const auto *tablet = static_cast<QTabletEvent *>(event);
        const InputDeviceInfo device = describeDevice(tablet->pointingDevice());
        emit proximityChanged({type == QEvent::TabletEnterProximity, device.tool}, device);
    }
    return false; // never swallow events
}

} // namespace mothion::input
