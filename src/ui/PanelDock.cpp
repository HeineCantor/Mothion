#include "ui/PanelDock.h"

#include "ui/IconFactory.h"

#include <QHBoxLayout>
#include <QLabel>
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

} // namespace

PanelDock::PanelDock(const QString &title, const QString &objectName, QWidget *content,
                     QWidget *parent)
    : QDockWidget(title, parent)
{
    setObjectName(objectName);
    setWidget(content);
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
    m_collapseButton = makeTitleButton(IconId::Collapse, tr("Riduci"), m_titleBar);
    auto *floatButton = makeTitleButton(IconId::Float, tr("Sgancia / aggancia"), m_titleBar);
    auto *closeButton = makeTitleButton(IconId::Close, tr("Chiudi"), m_titleBar);

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
    m_collapseButton->setToolTip(m_collapsed ? tr("Espandi") : tr("Riduci"));
}

} // namespace mothion::ui
