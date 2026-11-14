#include "ui/SelectionBar.h"

#include "canvas/CanvasView.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"
#include "ui/IconFactory.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QInputDialog>
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
    add(IconId::Deselect, tr("Deselect"), [this] { m_document->clearSelection(); })
        ->setObjectName(QStringLiteral("deselect"));
    add(IconId::InvertSelection, tr("Invert selection"), [this] { m_document->invertSelection(); })
        ->setObjectName(QStringLiteral("invert"));
    add(IconId::ExpandSelection, tr("Expand selection…"), [this] { grow(true); })
        ->setObjectName(QStringLiteral("expand"));
    add(IconId::ShrinkSelection, tr("Shrink selection…"), [this] { grow(false); })
        ->setObjectName(QStringLiteral("shrink"));
    add(IconId::EraseSelection, tr("Erase the selected pixels"), [this] { runWithReason(&Controller::eraseSelection); })
        ->setObjectName(QStringLiteral("erase"));
    add(IconId::EraseOutsideSelection, tr("Erase outside the selection"),
        [this] { runWithReason(&Controller::eraseOutsideSelection); })
        ->setObjectName(QStringLiteral("eraseOutside"));
    add(IconId::CutToLayer, tr("Cut to a new layer"), [this] { runWithReason(&Controller::cutSelectionToLayer); })
        ->setObjectName(QStringLiteral("cut"));
    add(IconId::CopyToLayer, tr("Copy to a new layer"), [this] { runWithReason(&Controller::copySelectionToLayer); })
        ->setObjectName(QStringLiteral("copy"));
    add(IconId::Transform, tr("Transform (Ctrl+T)"),
        [toolState] { toolState->setActiveTool(tools::ToolId::Transform); })
        ->setObjectName(QStringLiteral("transform"));
    add(IconId::FillSelection, tr("Fill the selection with the drawing color"), [this, toolState] {
        QString reason;
        const bool done = toolState->transparentSelected() ? m_document->eraseSelection(&reason)
                                                           : m_document->fillSelection(toolState->color(), &reason);
        if (!done && !reason.isEmpty())
            emit statusMessage(reason);
    })->setObjectName(QStringLiteral("fill"));
    add(IconId::FlipHorizontal, tr("Flip horizontally"), [this] { runWithReason(&Controller::flipSelectionHorizontally); })
        ->setObjectName(QStringLiteral("flipHorizontal"));
    add(IconId::FlipVertical, tr("Flip vertically"), [this] { runWithReason(&Controller::flipSelectionVertically); })
        ->setObjectName(QStringLiteral("flipVertical"));

    connect(m_document, &Controller::selectionChanged, this, &SelectionBar::place);
    connect(m_document, &Controller::documentReplaced, this, &SelectionBar::place);
    connect(m_canvas, &canvas::CanvasView::viewChanged, this, &SelectionBar::place);
    connect(m_canvas, &canvas::CanvasView::transformChanged, this, &SelectionBar::place);
    connect(m_canvas, &canvas::CanvasView::puppetChanged, this, &SelectionBar::place);
    m_canvas->installEventFilter(this); // its resizes
    place();
}

bool SelectionBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize)
        place();
    return QFrame::eventFilter(watched, event);
}

void SelectionBar::runWithReason(bool (document::DocumentController::*command)(QString *))
{
    QString reason;
    if (!(m_document->*command)(&reason))
        emit statusMessage(reason);
}

void SelectionBar::grow(bool out)
{
    bool ok = false;
    const int pixels = QInputDialog::getInt(m_canvas->window(), out ? tr("Expand selection") : tr("Shrink selection"),
                                            tr("Width (px):"), m_growPixels, 1, 1000, 1, &ok);
    if (!ok)
        return;
    m_growPixels = pixels;
    QString reason;
    if (!m_document->growSelection(out ? pixels : -pixels, &reason) && !reason.isEmpty())
        emit statusMessage(reason);
}

void SelectionBar::place()
{
    // While a transform or a puppet warp is open its own bar (OK / Cancel) takes this place.
    if (!m_document->hasSelection() || !m_canvas->transformBounds().isNull() || !m_canvas->puppetBounds().isNull()) {
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
