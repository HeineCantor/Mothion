#include "ui/panels/timeline/TimelinePanel.h"

#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"
#include "ui/IconFactory.h"
#include "ui/panels/BlendModes.h"
#include "ui/panels/SliderField.h"
#include "ui/panels/timeline/TimelineView.h"

#include <QColorDialog>
#include <QComboBox>
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

QToolButton *celButton(IconId id, const QString &name, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(name);
    b->setIcon(icon(id));
    b->setIconSize(QSize(16, 16));
    b->setAutoRaise(true);
    b->setToolTip(tip);
    return b;
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
    layout->addLayout(celBar());
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
                                                m_document->currentFrame(), 1, tr("Layer 1").toStdString())
                                .command);
    });
    connect(zoom, &QSlider::valueChanged, m_view, &TimelineView::setFrameWidth);
    connect(m_view, &TimelineView::frameWidthChanged, zoom, &QSlider::setValue);
}

QLayout *TimelinePanel::celBar()
{
    auto *add = celButton(IconId::Add, QStringLiteral("celAdd"), tr("New cel above the current one"), this);
    auto *remove = celButton(IconId::Delete, QStringLiteral("celRemove"), tr("Delete the current cel"), this);
    auto *up = celButton(IconId::ArrowUp, QStringLiteral("celUp"), tr("Move the current cel up"), this);
    auto *down = celButton(IconId::ArrowDown, QStringLiteral("celDown"), tr("Move the current cel down"), this);
    auto *blend = new QComboBox(this);
    blend->setObjectName(QStringLiteral("celBlend"));
    blend->setToolTip(tr("How the current cel's frames go over the cels below"));
    for (doc::BlendMode mode : kBlendModes)
        blend->addItem(blendModeName(mode));
    auto *opacity = new QSlider(Qt::Horizontal, this);
    opacity->setObjectName(QStringLiteral("celOpacity"));
    opacity->setRange(0, 100);
    opacity->setMaximumWidth(160);
    auto *opacityValue = new QSpinBox(this);
    opacityValue->setObjectName(QStringLiteral("celOpacityValue"));
    opacityValue->setRange(0, 100);
    opacityValue->setSuffix(QStringLiteral(" %"));
    opacityValue->setButtonSymbols(QAbstractSpinBox::NoButtons);
    opacityValue->setKeyboardTracking(false);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(4, 0, 4, 2);
    row->setSpacing(4);
    row->addWidget(new QLabel(tr("Cel"), this));
    for (QWidget *w : std::initializer_list<QWidget *>{add, remove, up, down})
        row->addWidget(w);
    row->addSpacing(8);
    row->addWidget(blend);
    row->addWidget(opacity, 1);
    row->addWidget(opacityValue);
    row->addStretch(1);

    // Every edit is one undo step on the current cel.
    auto edit = [this](auto change) {
        const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
        if (!cel)
            return;
        doc::CelProperties p = cel->properties();
        change(p);
        m_document->execute(doc::makeSetCelProperties(m_document->document(), cel->id(), p));
    };
    connect(add, &QToolButton::clicked, m_document, &DocumentController::addCel);
    connect(remove, &QToolButton::clicked, this, [this] {
        if (m_document->document().celCount() > 1) // keep at least one
            m_document->execute(std::make_unique<doc::RemoveCelCommand>(m_document->currentCel()));
    });
    auto move = [this](int direction) {
        const doc::Document &d = m_document->document();
        const int to = d.indexOf(m_document->currentCel()) + direction;
        if (to >= 0 && to < d.celCount())
            m_document->execute(std::make_unique<doc::MoveCelCommand>(m_document->currentCel(), to));
    };
    connect(up, &QToolButton::clicked, this, [move] { move(+1); });
    connect(down, &QToolButton::clicked, this, [move] { move(-1); });
    connect(blend, &QComboBox::activated, this,
            [edit](int i) { edit([i](doc::CelProperties &p) { p.blend = kBlendModes[std::size_t(i)]; }); });
    // The slider previews in the spin box while dragged and commits on
    // release, so a drag is one undo step.
    auto commitOpacity = [edit, opacity] {
        const float value = float(opacity->value()) / 100.0f;
        edit([value](doc::CelProperties &p) { p.opacity = value; });
    };
    connect(opacity, &QSlider::valueChanged, this, [opacity, opacityValue, commitOpacity](int v) {
        const QSignalBlocker block(opacityValue);
        opacityValue->setValue(v);
        if (!opacity->isSliderDown())
            commitOpacity();
    });
    connect(opacity, &QSlider::sliderReleased, this, commitOpacity);
    connect(opacityValue, &QSpinBox::valueChanged, this, [opacity, commitOpacity](int v) {
        const QSignalBlocker block(opacity);
        opacity->setValue(v);
        commitOpacity();
    });

    auto sync = [=, this] {
        const doc::Document &d = m_document->document();
        const doc::Cel *cel = d.cel(m_document->currentCel());
        const int index = cel ? d.indexOf(cel->id()) : -1;
        remove->setEnabled(cel && d.celCount() > 1);
        up->setEnabled(cel && index + 1 < d.celCount());
        down->setEnabled(cel && index > 0);
        for (QWidget *w : std::initializer_list<QWidget *>{blend, opacity, opacityValue})
            w->setEnabled(cel != nullptr);
        if (!cel)
            return;
        const QSignalBlocker b1(blend), b2(opacity), b3(opacityValue);
        blend->setCurrentIndex(blendModeIndex(cel->properties().blend));
        opacity->setValue(int(std::lround(cel->properties().opacity * 100)));
        opacityValue->setValue(opacity->value());
    };
    connect(m_document, &DocumentController::currentCelChanged, this, sync);
    connect(m_document, &DocumentController::structureChanged, this, sync);
    sync();
    return row;
}

} // namespace mothion::ui
