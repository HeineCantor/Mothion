#include "canvas/DragThrottle.h"

#include <QTimer>

#include <algorithm>

namespace mothion::canvas {

DragThrottle::DragThrottle() = default;
DragThrottle::~DragThrottle() = default;

void DragThrottle::request(std::function<void()> draw)
{
    m_pending = std::move(draw);
    const qint64 wait = std::max<qint64>(m_drawCost, 8); // at most about 120 drawings a second
    const qint64 since = m_lastDraw.isValid() ? m_lastDraw.elapsed() : wait;
    if (since >= wait) {
        run();
        return;
    }
    if (!m_timer) {
        m_timer = std::make_unique<QTimer>();
        m_timer->setSingleShot(true);
        QObject::connect(m_timer.get(), &QTimer::timeout, m_timer.get(), [this] {
            run();
            if (m_drawnLater)
                m_drawnLater();
        });
    }
    if (!m_timer->isActive())
        m_timer->start(int(wait - since));
}

void DragThrottle::flush()
{
    run();
}

void DragThrottle::cancel()
{
    if (m_timer)
        m_timer->stop();
    m_pending = nullptr;
}

void DragThrottle::run()
{
    if (m_timer)
        m_timer->stop();
    if (!m_pending)
        return;
    const std::function<void()> draw = std::move(m_pending);
    m_pending = nullptr;
    QElapsedTimer cost;
    cost.start();
    draw();
    m_drawCost = cost.elapsed();
    m_lastDraw.start();
}

} // namespace mothion::canvas
