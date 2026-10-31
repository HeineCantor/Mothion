#include "ui/panels/ToolPalettePanel.h"

#include "ui/IconFactory.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QToolButton>
#include <QVBoxLayout>

namespace mothion::ui {

using tools::ToolId;

ToolPalettePanel::ToolPalettePanel(tools::ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
{
    struct Entry { ToolId tool; IconId icon; QKeySequence shortcut; };
    const Entry entries[] = {
        {ToolId::Brush, IconId::Brush, QKeySequence(Qt::Key_B)},
        {ToolId::Eraser, IconId::Eraser, QKeySequence(Qt::Key_E)},
        {ToolId::Hand, IconId::Hand, QKeySequence(Qt::Key_H)},
        {ToolId::Zoom, IconId::Zoom, QKeySequence(Qt::Key_Z)},
    };

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    for (const Entry &e : entries) {
        auto *button = new QToolButton(this);
        button->setIcon(icon(e.icon));
        button->setIconSize(QSize(24, 24));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setShortcut(e.shortcut);
        button->setToolTip(QStringLiteral("%1 (%2)").arg(tools::ToolState::displayName(e.tool),
                                                         e.shortcut.toString()));
        m_group->addButton(button, static_cast<int>(e.tool));
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }
    layout->addStretch(1);

    m_group->button(static_cast<int>(state->activeTool()))->setChecked(true);
    connect(m_group, &QButtonGroup::idClicked, state,
            [state](int id) { state->setActiveTool(static_cast<ToolId>(id)); });
    connect(state, &tools::ToolState::activeToolChanged, this,
            [this](ToolId tool) { m_group->button(static_cast<int>(tool))->setChecked(true); });
}

} // namespace mothion::ui
