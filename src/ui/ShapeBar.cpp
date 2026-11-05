#include "ui/ShapeBar.h"

#include "canvas/CanvasView.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QToolButton>

#include <algorithm>

namespace mothion::ui {

namespace {

constexpr int kGap = 12;   // between the points and the bar
constexpr int kMargin = 4; // between the bar and the canvas edges

} // namespace

ShapeBar::ShapeBar(canvas::CanvasView *canvas)
    : QFrame(canvas)
    , m_canvas(canvas)
{
    setObjectName(QStringLiteral("ShapeBar"));
    setFrameShape(QFrame::StyledPanel);
    setAutoFillBackground(true);
    setAttribute(Qt::WA_NoMousePropagation); // clicks stay here, they place no point
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
        connect(button, &QToolButton::clicked, this, [this, apply] { m_canvas->finishShape(apply); });
        layout->addWidget(button);
        return button;
    };
    m_ok = add(tr("OK"), tr("Finish the shape (Enter)"), QStringLiteral("ok"), true);
    add(tr("Cancel"), tr("Drop the shape (Esc)"), QStringLiteral("cancel"), false);

    connect(m_canvas, &canvas::CanvasView::shapeChanged, this, &ShapeBar::place);
    connect(m_canvas, &canvas::CanvasView::viewChanged, this, &ShapeBar::place);
    m_canvas->installEventFilter(this); // its resizes
    place();
}

bool ShapeBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize)
        place();
    return QFrame::eventFilter(watched, event);
}

void ShapeBar::place()
{
    const QRectF box = m_canvas->shapeBounds();
    if (box.isNull()) {
        hide();
        return;
    }
    m_ok->setEnabled(m_canvas->shapeCanFinish());
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
