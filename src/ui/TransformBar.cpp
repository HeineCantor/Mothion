#include "ui/TransformBar.h"

#include "canvas/CanvasView.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QToolButton>

#include <algorithm>

namespace mothion::ui {

namespace {

constexpr int kGap = 8;    // between the box and the bar
constexpr int kMargin = 4; // between the bar and the canvas edges

} // namespace

TransformBar::TransformBar(canvas::CanvasView *canvas)
    : QFrame(canvas)
    , m_canvas(canvas)
{
    setObjectName(QStringLiteral("TransformBar"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setAttribute(Qt::WA_NoMousePropagation); // as the selection bar: clicks stay here
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(2);

    auto add = [this, layout](const QString &text, const QString &tip, const QString &name, bool apply) {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setObjectName(name);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus); // the keys stay with the canvas
        connect(button, &QToolButton::clicked, this, [this, apply] { m_canvas->finishTransform(apply); });
        layout->addWidget(button);
    };
    add(tr("OK"), tr("Apply the transform (Enter)"), QStringLiteral("ok"), true);
    add(tr("Cancel"), tr("Drop the transform (Esc)"), QStringLiteral("cancel"), false);

    connect(m_canvas, &canvas::CanvasView::transformChanged, this, &TransformBar::place);
    connect(m_canvas, &canvas::CanvasView::viewChanged, this, &TransformBar::place);
    m_canvas->installEventFilter(this); // its resizes
    place();
}

bool TransformBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize)
        place();
    return QFrame::eventFilter(watched, event);
}

void TransformBar::place()
{
    const QRectF box = m_canvas->transformBounds();
    if (box.isNull()) {
        hide();
        return;
    }
    resize(sizeHint());
    const QRect area = m_canvas->view().documentToView().mapRect(box).toAlignedRect();
    const QRect bounds = m_canvas->rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
    int y = area.bottom() + kGap;
    if (y + height() > bounds.bottom() && area.top() - kGap - height() >= bounds.top())
        y = area.top() - kGap - height(); // no room below
    y = std::clamp(y, bounds.top(), std::max(bounds.top(), bounds.bottom() - height()));
    const int x =
        std::clamp(area.center().x() - width() / 2, bounds.left(), std::max(bounds.left(), bounds.right() - width()));
    move(x, y);
    show();
    raise();
}

} // namespace mothion::ui
