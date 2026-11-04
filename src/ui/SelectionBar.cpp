#include "ui/SelectionBar.h"

#include "canvas/CanvasView.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"
#include "ui/IconFactory.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QToolButton>

#include <algorithm>

namespace mothion::ui {

namespace {

constexpr int kGap = 8;    // between the selection and the bar
constexpr int kMargin = 4; // between the bar and the canvas edges

} // namespace

SelectionBar::SelectionBar(canvas::CanvasView *canvas, document::DocumentController *document,
                           tools::ToolState *toolState)
    : QFrame(canvas)
    , m_canvas(canvas)
    , m_document(document)
{
    setObjectName(QStringLiteral("SelectionBar"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    // Pen and mouse events the buttons ignore must not reach the canvas
    // underneath: it would accept a pen press, so Qt would never turn it
    // into the click, and the active tool would act instead.
    setAttribute(Qt::WA_NoMousePropagation);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(2);

    using Controller = document::DocumentController;
    auto add = [this, layout](IconId id, const QString &tip, auto run) {
        auto *button = new QToolButton(this);
        button->setIcon(icon(id));
        button->setIconSize(QSize(20, 20));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus); // the keys stay with the canvas
        connect(button, &QToolButton::clicked, this, run);
        layout->addWidget(button);
        return button;
    };
    // Those that may fail say why in the status bar.
    auto withReason = [this](bool (Controller::*command)(QString *)) {
        return [this, command] {
            QString reason;
            if (!(m_document->*command)(&reason))
                emit statusMessage(reason);
        };
    };
    add(IconId::Deselect, tr("Deselect"), [this] { m_document->clearSelection(); })
        ->setObjectName(QStringLiteral("deselect"));
    add(IconId::InvertSelection, tr("Invert selection"), [this] { m_document->invertSelection(); })
        ->setObjectName(QStringLiteral("invert"));
    add(IconId::EraseSelection, tr("Erase the selected pixels"), withReason(&Controller::eraseSelection))
        ->setObjectName(QStringLiteral("erase"));
    add(IconId::CutToLayer, tr("Cut to a new layer"), withReason(&Controller::cutSelectionToLayer))
        ->setObjectName(QStringLiteral("cut"));
    add(IconId::CopyToLayer, tr("Copy to a new layer"), withReason(&Controller::copySelectionToLayer))
        ->setObjectName(QStringLiteral("copy"));
    add(IconId::Transform, tr("Transform (Ctrl+T)"),
        [toolState] { toolState->setActiveTool(tools::ToolId::Transform); })
        ->setObjectName(QStringLiteral("transform"));

    connect(m_document, &Controller::selectionChanged, this, &SelectionBar::place);
    connect(m_document, &Controller::documentReplaced, this, &SelectionBar::place);
    connect(m_canvas, &canvas::CanvasView::viewChanged, this, &SelectionBar::place);
    connect(m_canvas, &canvas::CanvasView::transformChanged, this, &SelectionBar::place);
    m_canvas->installEventFilter(this); // its resizes
    place();
}

bool SelectionBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize)
        place();
    return QFrame::eventFilter(watched, event);
}

void SelectionBar::place()
{
    // While a transform is open its own bar (OK / Cancel) takes this place.
    if (!m_document->hasSelection() || !m_canvas->transformBounds().isNull()) {
        hide();
        return;
    }
    resize(sizeHint());
    const QRect area = m_canvas->view().documentToView().mapRect(m_document->selection().boundingRect()).toAlignedRect();
    const QRect bounds = m_canvas->rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
    int y = area.bottom() + kGap;
    if (y + height() > bounds.bottom() && area.top() - kGap - height() >= bounds.top())
        y = area.top() - kGap - height(); // no room below
    y = std::clamp(y, bounds.top(), std::max(bounds.top(), bounds.bottom() - height()));
    const int x = std::clamp(area.center().x() - width() / 2, bounds.left(), std::max(bounds.left(), bounds.right() - width()));
    move(x, y);
    show();
    raise();
}

} // namespace mothion::ui
