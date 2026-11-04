#include "ui/PanelDock.h"

#include "ui/IconFactory.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QToolButton>

namespace mothion::ui {

namespace {

QToolButton *makeTitleButton(IconId id, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(id));
    b->setIconSize(QSize(12, 12));
    b->setAutoRaise(true);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

// Holds a panel's content. The content gets the whole visible area, never
// less than its minimum size; when the dock is smaller than that (a short
// or highly scaled screen), the rest scrolls instead of spilling under the
// next dock. Unlike QScrollArea's widgetResizable, height-for-width is not
// applied, so square widgets keep fitting the height they are given, as
// when the content sat in the dock directly.
class PanelScrollArea : public QScrollArea
{
public:
    explicit PanelScrollArea(QWidget *content, QWidget *parent)
        : QScrollArea(parent)
    {
        setWidget(content); // also makes QScrollArea watch the content's events
        setFrameShape(QFrame::NoFrame);
        viewport()->setAutoFillBackground(false);
        content->setAutoFillBackground(false);
    }

    // The dock area sizes docks from the content, as before.
    QSize sizeHint() const override { return widget()->sizeHint(); }

protected:
    bool viewportEvent(QEvent *event) override
    {
        if (event->type() == QEvent::Resize)
            fitContent();
        return QScrollArea::viewportEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const bool result = QScrollArea::eventFilter(watched, event);
        if (watched == widget() && event->type() == QEvent::LayoutRequest)
            fitContent(); // the content's minimum may have changed
        return result;
    }

private:
    void fitContent()
    {
        QWidget *content = widget();
        const QSize minimum = content->minimumSizeHint().expandedTo(content->minimumSize());
        content->resize(viewport()->size().expandedTo(minimum));
    }
};

} // namespace

PanelDock::PanelDock(const QString &title, const QString &objectName, QWidget *content,
                     QWidget *parent, QWidget *footer)
    : QDockWidget(title, parent)
{
    setObjectName(objectName);
    // Scrolls when the dock area has less room than the panel needs.
    auto *scroll = new PanelScrollArea(content, this);
    if (footer) {
        auto *holder = new QWidget(this);
        auto *layout = new QVBoxLayout(holder);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(scroll, 1);
        layout->addWidget(footer);
        setWidget(holder);
    } else {
        setWidget(scroll);
    }
    setFeatures(DockWidgetMovable | DockWidgetFloatable | DockWidgetClosable);

    // Custom title bar. Mouse presses that the buttons don't take fall through
    // to QDockWidget, so dragging / docking keeps working as usual.
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("PanelTitleBar"));
    m_titleBar->setAttribute(Qt::WA_StyledBackground);
    auto *label = new QLabel(title, m_titleBar);
    label->setObjectName(QStringLiteral("PanelTitle"));
    // Let the title shrink, so narrow panels (the tool palette) are not
    // forced wider than their content by the title text.
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_collapseButton = makeTitleButton(IconId::Collapse, tr("Zoom out"), m_titleBar);
    auto *floatButton = makeTitleButton(IconId::Float, tr("Float / dock"), m_titleBar);
    auto *closeButton = makeTitleButton(IconId::Close, tr("Close"), m_titleBar);

    auto *row = new QHBoxLayout(m_titleBar);
    row->setContentsMargins(2, 1, 2, 1);
    row->setSpacing(0);
    row->addWidget(label, 1);
    row->addWidget(m_collapseButton);
    row->addWidget(floatButton);
    row->addWidget(closeButton);
    setTitleBarWidget(m_titleBar);

    connect(m_collapseButton, &QToolButton::clicked, this, [this] { setCollapsed(!m_collapsed); });
    connect(floatButton, &QToolButton::clicked, this, [this] { setFloating(!isFloating()); });
    connect(closeButton, &QToolButton::clicked, this, &QDockWidget::close);
    connect(this, &QDockWidget::windowTitleChanged, label, &QLabel::setText);
}

void PanelDock::setCollapsed(bool collapsed)
{
    if (collapsed == m_collapsed)
        return;
    m_collapsed = collapsed;
    if (widget())
        widget()->setVisible(!collapsed);
    // Without a cap the dock area would keep the old height as empty space.
    setMaximumHeight(collapsed ? m_titleBar->sizeHint().height() : QWIDGETSIZE_MAX);
    updateButtons();
}

void PanelDock::updateButtons()
{
    m_collapseButton->setIcon(icon(m_collapsed ? IconId::Expand : IconId::Collapse));
    m_collapseButton->setToolTip(m_collapsed ? tr("Expand") : tr("Zoom out"));
}

} // namespace mothion::ui
