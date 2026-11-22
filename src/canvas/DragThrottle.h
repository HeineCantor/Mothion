#pragma once

#include <QElapsedTimer>

#include <functional>
#include <memory>

class QTimer;

namespace mothion::canvas {

// For a drag whose every step takes time to draw (a deformer bending the drawings again): a pen sends far more samples
// than can be drawn, so a step is drawn only once the last drawing's time has passed. The last step asked for is always
// drawn, later if need be (and then `drawnLater` is called, to show it).
class DragThrottle
{
public:
    DragThrottle();
    ~DragThrottle();
    void setDrawnLater(std::function<void()> drawnLater) { m_drawnLater = std::move(drawnLater); }

    // `draw` now, or later in place of any step asked for before it and not drawn yet.
    void request(std::function<void()> draw);
    // The step not drawn yet, now.
    void flush();
    // The step not drawn yet is forgotten.
    void cancel();

private:
    void run();

    std::function<void()> m_pending;
    std::function<void()> m_drawnLater;
    QElapsedTimer m_lastDraw;
    qint64 m_drawCost = 0; // ms the last drawing took
    std::unique_ptr<QTimer> m_timer;
};

} // namespace mothion::canvas
