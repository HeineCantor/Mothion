#pragma once

#include "core/PenSample.h"

#include <QObject>

namespace mothion::input {

// Qt delivers TabletEnterProximity / TabletLeaveProximity only to the
// QApplication object, never to widgets. This filter, installed on the
// application, re-emits them as ProximityEvents.
class ProximityFilter : public QObject
{
    Q_OBJECT

public:
    explicit ProximityFilter(QObject *parent = nullptr); // installs itself on qApp

signals:
    void proximityChanged(const mothion::ProximityEvent &event,
                          const mothion::InputDeviceInfo &device);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

} // namespace mothion::input
