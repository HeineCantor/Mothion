#pragma once

#include "core/PenSample.h"
#include "core/RateMeter.h"

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace mothion::canvas {

// Text panel over the canvas (toggled with F12) showing live input data:
// tablet backend, device, capabilities, proximity, event rate and the
// values of the last PenSample.
//
// It is transparent to mouse/tablet input and repaints on a timer
// (not per sample), so showing it does not disturb what it measures.
class DebugOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit DebugOverlay(QWidget *parent);

    void addSample(const PenSample &sample);
    void setDevice(const InputDeviceInfo &device);
    void setProximity(const ProximityEvent &event);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refresh(); // rebuilds the text and fits the widget to it
    QStringList lines();
    QFont font() const;
    double seconds() const { return double(m_clock.nsecsElapsed()) / 1e9; }

    QTimer m_refresh;
    QStringList m_text;
    QElapsedTimer m_clock;
    RateMeter m_rate;
    PenSample m_last;
    InputDeviceInfo m_device;
    QString m_backend;
    bool m_inProximity = false;
    bool m_proximityKnown = false;
    quint64 m_totalSamples = 0;
    quint64 m_strokeSamples = 0;
};

} // namespace mothion::canvas
