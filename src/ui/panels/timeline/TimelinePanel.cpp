#include "ui/panels/timeline/TimelinePanel.h"

#include "ui/panels/timeline/TimelineView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace mothion::ui {

TimelinePanel::TimelinePanel(QWidget *parent)
    : QWidget(parent)
    , m_view(new TimelineView(this))
{
    m_view->setTimeline(makeFakeTimeline());

    auto *prev = new QToolButton(this);
    prev->setArrowType(Qt::LeftArrow);
    prev->setAutoRaise(true);
    prev->setToolTip(tr("Frame precedente"));
    auto *next = new QToolButton(this);
    next->setArrowType(Qt::RightArrow);
    next->setAutoRaise(true);
    next->setToolTip(tr("Frame successivo"));
    auto *frameLabel = new QLabel(this);
    frameLabel->setMinimumWidth(150);

    auto *zoom = new QSlider(Qt::Horizontal, this);
    zoom->setRange(6, 60);
    zoom->setValue(m_view->frameWidth());
    zoom->setMaximumWidth(140);
    zoom->setToolTip(tr("Larghezza frame (Ctrl+rotella sulla timeline)"));

    auto *note = new QLabel(tr("Segnaposto – dati finti"), this);
    note->setEnabled(false);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(4, 2, 4, 2);
    bar->setSpacing(4);
    bar->addWidget(prev);
    bar->addWidget(next);
    bar->addWidget(frameLabel);
    bar->addStretch(1);
    bar->addWidget(note);
    bar->addSpacing(12);
    bar->addWidget(new QLabel(tr("Zoom"), this));
    bar->addWidget(zoom);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bar);
    layout->addWidget(m_view, 1);

    auto showFrame = [this, frameLabel](int frame) {
        const auto &t = m_view->timeline();
        frameLabel->setText(tr("Frame %1 / %2  ·  %3 fps").arg(frame + 1).arg(t.frameCount).arg(t.fps));
    };
    showFrame(m_view->currentFrame());
    connect(m_view, &TimelineView::currentFrameChanged, this, showFrame);
    connect(prev, &QToolButton::clicked, m_view, [this] { m_view->setCurrentFrame(m_view->currentFrame() - 1); });
    connect(next, &QToolButton::clicked, m_view, [this] { m_view->setCurrentFrame(m_view->currentFrame() + 1); });
    connect(zoom, &QSlider::valueChanged, m_view, &TimelineView::setFrameWidth);
    connect(m_view, &TimelineView::frameWidthChanged, zoom, &QSlider::setValue);
}

} // namespace mothion::ui
