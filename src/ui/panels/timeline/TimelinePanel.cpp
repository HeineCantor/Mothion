#include "ui/panels/timeline/TimelinePanel.h"

#include "core/document/commands/DrawingCommands.h"
#include "document/DocumentController.h"
#include "ui/panels/timeline/TimelineView.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace mothion::ui {

using document::DocumentController;

TimelinePanel::TimelinePanel(DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_view(new TimelineView(document, this))
{
    auto *prev = new QToolButton(this);
    prev->setArrowType(Qt::LeftArrow);
    prev->setAutoRaise(true);
    prev->setToolTip(tr("Previous frame"));
    auto *next = new QToolButton(this);
    next->setArrowType(Qt::RightArrow);
    next->setAutoRaise(true);
    next->setToolTip(tr("Next frame"));
    auto *frameLabel = new QLabel(this);
    frameLabel->setMinimumWidth(150);

    auto *newDrawing = new QPushButton(tr("New drawing"), this);
    newDrawing->setToolTip(tr("Create an empty drawing at the current frame of the current layer"));

    auto *zoom = new QSlider(Qt::Horizontal, this);
    zoom->setRange(6, 60);
    zoom->setValue(m_view->frameWidth());
    zoom->setMaximumWidth(140);
    zoom->setToolTip(tr("Frame width (Ctrl+wheel on the timeline)"));

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(4, 2, 4, 2);
    bar->setSpacing(4);
    bar->addWidget(prev);
    bar->addWidget(next);
    bar->addWidget(frameLabel);
    bar->addWidget(newDrawing);
    bar->addStretch(1);
    bar->addWidget(new QLabel(tr("Zoom"), this));
    bar->addWidget(zoom);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bar);
    layout->addWidget(m_view, 1);

    auto showFrame = [this, frameLabel] {
        const doc::Document &d = m_document->document();
        frameLabel->setText(tr("Frame %1 / %2  ·  %3 fps")
                                .arg(m_document->currentFrame() + 1)
                                .arg(d.frameCount())
                                .arg(d.frameRate().fps(), 0, 'g', 5));
    };
    showFrame();
    connect(m_document, &DocumentController::currentFrameChanged, this, showFrame);
    connect(m_document, &DocumentController::structureChanged, this, showFrame);
    connect(prev, &QToolButton::clicked, m_document,
            [this] { m_document->setCurrentFrame(m_document->currentFrame() - 1); });
    connect(next, &QToolButton::clicked, m_document,
            [this] { m_document->setCurrentFrame(m_document->currentFrame() + 1); });
    connect(newDrawing, &QPushButton::clicked, m_document, [this] {
        m_document->execute(doc::makeNewDrawing(m_document->documentForCommands(), m_document->currentLayer(),
                                                m_document->currentFrame())
                                .command);
    });
    connect(zoom, &QSlider::valueChanged, m_view, &TimelineView::setFrameWidth);
    connect(m_view, &TimelineView::frameWidthChanged, zoom, &QSlider::setValue);
}

} // namespace mothion::ui
