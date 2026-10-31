#pragma once

#include <QWidget>

namespace mothion::ui {

class TimelineView;

// Placeholder timeline panel: a small header bar (frame counter, frame
// stepping, zoom) above a TimelineView filled with fake data.
class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    explicit TimelinePanel(QWidget *parent = nullptr);

    TimelineView *view() const { return m_view; }

private:
    TimelineView *m_view;
};

} // namespace mothion::ui
