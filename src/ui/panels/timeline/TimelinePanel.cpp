#include "ui/panels/timeline/TimelinePanel.h"

#include "core/document/commands/DrawingCommands.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"
#include "ui/panels/SliderField.h"
#include "ui/panels/timeline/TimelineView.h"

#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QWidgetAction>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace mothion::ui {

using document::DocumentController;

namespace {

QIcon swatch(const QColor &color)
{
    QPixmap pm(28, 14);
    pm.fill(color);
    QPainter p(&pm);
    p.setPen(QColor(0, 0, 0, 120));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return QIcon(pm);
}

// The onion skin's settings, edited in place in ToolState.
QWidget *onionSkinSettings(tools::ToolState *state, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *before = new QSpinBox(box);
    auto *after = new QSpinBox(box);
    for (QSpinBox *spin : {before, after})
        spin->setRange(0, tools::ToolState::kMaxOnionFrames);
    before->setObjectName(QStringLiteral("onionBefore"));
    after->setObjectName(QStringLiteral("onionAfter"));
    auto *beforeColor = new QToolButton(box);
    auto *afterColor = new QToolButton(box);
    beforeColor->setObjectName(QStringLiteral("onionBeforeColor"));
    afterColor->setObjectName(QStringLiteral("onionAfterColor"));
    for (QToolButton *b : {beforeColor, afterColor})
        b->setIconSize(QSize(28, 14));
    auto *opacity = new SliderField(TimelinePanel::tr("Opacity"), 0, 100, QStringLiteral(" %"), box);
    opacity->setObjectName(QStringLiteral("onionOpacity"));

    auto *form = new QFormLayout(box);
    form->setContentsMargins(8, 6, 8, 6);
    form->addRow(TimelinePanel::tr("Frames before"), before);
    form->addRow(TimelinePanel::tr("Frames after"), after);
    form->addRow(TimelinePanel::tr("Before color"), beforeColor);
    form->addRow(TimelinePanel::tr("After color"), afterColor);
    form->addRow(opacity);

    auto edit = [state](auto change) {
        tools::OnionSkinSettings s = state->onionSkin();
        change(s);
        state->setOnionSkin(s);
    };
    QObject::connect(before, &QSpinBox::valueChanged, box,
                     [edit](int v) { edit([v](tools::OnionSkinSettings &s) { s.before = v; }); });
    QObject::connect(after, &QSpinBox::valueChanged, box,
                     [edit](int v) { edit([v](tools::OnionSkinSettings &s) { s.after = v; }); });
    QObject::connect(opacity, &SliderField::valueEdited, box,
                     [edit](int v) { edit([v](tools::OnionSkinSettings &s) { s.opacity = v / 100.0; }); });
    auto pick = [state, edit, box](bool isBefore) {
        const tools::OnionSkinSettings s = state->onionSkin();
        const QColor chosen = QColorDialog::getColor(isBefore ? s.beforeColor : s.afterColor, box,
                                                     isBefore ? TimelinePanel::tr("Onion skin: frames before")
                                                              : TimelinePanel::tr("Onion skin: frames after"));
        if (chosen.isValid())
            edit([chosen, isBefore](tools::OnionSkinSettings &o) { (isBefore ? o.beforeColor : o.afterColor) = chosen; });
    };
    QObject::connect(beforeColor, &QToolButton::clicked, box, [pick] { pick(true); });
    QObject::connect(afterColor, &QToolButton::clicked, box, [pick] { pick(false); });

    auto sync = [=] {
        const tools::OnionSkinSettings s = state->onionSkin();
        const QSignalBlocker b1(before), b2(after);
        before->setValue(s.before);
        after->setValue(s.after);
        opacity->setValue(int(std::lround(s.opacity * 100)));
        beforeColor->setIcon(swatch(s.beforeColor));
        afterColor->setIcon(swatch(s.afterColor));
    };
    QObject::connect(state, &tools::ToolState::onionSkinChanged, box, sync);
    sync();
    return box;
}

} // namespace

TimelinePanel::TimelinePanel(DocumentController *document, tools::ToolState *toolState, QWidget *parent)
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

    auto *newDrawing = new QPushButton(tr("New frame"), this);
    newDrawing->setToolTip(tr("Create an empty frame at this point of the current cel"));

    // Onion skin: the button turns it on, its arrow opens the settings.
    auto *onion = new QToolButton(this);
    onion->setObjectName(QStringLiteral("onionSkin"));
    onion->setText(tr("Onion skin"));
    onion->setToolTip(tr("Show the frames around this one (the arrow: settings)"));
    onion->setCheckable(true);
    onion->setChecked(toolState->onionSkin().enabled);
    onion->setPopupMode(QToolButton::MenuButtonPopup);
    auto *menu = new QMenu(onion);
    auto *settings = new QWidgetAction(menu);
    settings->setDefaultWidget(onionSkinSettings(toolState, menu));
    menu->addAction(settings);
    onion->setMenu(menu);
    connect(onion, &QToolButton::toggled, toolState, [toolState](bool on) {
        tools::OnionSkinSettings s = toolState->onionSkin();
        s.enabled = on;
        toolState->setOnionSkin(s);
    });
    connect(toolState, &tools::ToolState::onionSkinChanged, onion,
            [onion, toolState] { onion->setChecked(toolState->onionSkin().enabled); });

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
    bar->addWidget(onion);
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
        m_document->execute(doc::makeNewDrawing(m_document->documentForCommands(), m_document->currentCel(),
                                                m_document->currentFrame())
                                .command);
    });
    connect(zoom, &QSlider::valueChanged, m_view, &TimelineView::setFrameWidth);
    connect(m_view, &TimelineView::frameWidthChanged, zoom, &QSlider::setValue);
}

} // namespace mothion::ui
