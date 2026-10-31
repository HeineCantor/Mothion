#pragma once

#include "ui/panels/timeline/FakeTimelineData.h"
#include "ui/panels/timeline/TimelineMetrics.h"

#include <QAbstractScrollArea>

namespace mothion::ui {

// Placeholder exposure timeline (inspired by Toon Boom Harmony's Xsheet/Timeline):
// one row per layer, frames horizontally, each exposure drawn as a block that
// spans its hold with the drawing name. A ruler on top shows frame numbers
// and the current-frame cursor. Click or drag in the ruler/cells to scrub.
//
// Everything is custom-painted in one widget so the ruler, names and cells
// scroll together without synchronising several widgets.
class TimelineView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TimelineView(QWidget *parent = nullptr);

    void setTimeline(FakeTimeline timeline);
    const FakeTimeline &timeline() const { return m_timeline; }

    int currentFrame() const { return m_currentFrame; }
    void setCurrentFrame(int frame);

    int frameWidth() const { return m_metrics.frameWidth; }
    void setFrameWidth(int width);

signals:
    void currentFrameChanged(int frame);
    void frameWidthChanged(int width);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateScrollBars();
    void ensureFrameVisible(int frame);

    void paintCells(QPainter &p) const;
    void paintExposure(QPainter &p, const Exposure &e, const QColor &color, int y) const;
    void paintRuler(QPainter &p) const;
    void paintHeaders(QPainter &p) const;
    void paintCursor(QPainter &p) const;

    FakeTimeline m_timeline;
    TimelineMetrics m_metrics;
    int m_currentFrame = 0;
    bool m_scrubbing = false;
};

} // namespace mothion::ui
