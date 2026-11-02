#include "ui/panels/ToolPalettePanel.h"

#include "ui/IconFactory.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QShortcut>
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
        {ToolId::Move, IconId::Move, QKeySequence(Qt::Key_V)},
        {ToolId::RectSelect, IconId::RectSelect, QKeySequence(Qt::Key_M)},
        {ToolId::LassoSelect, IconId::LassoSelect, QKeySequence(Qt::Key_L)},
        {ToolId::Puppet, IconId::Puppet, QKeySequence(Qt::Key_P)},
        {ToolId::Brush, IconId::Brush, QKeySequence(Qt::Key_B)},
        {ToolId::Eraser, IconId::Eraser, QKeySequence(Qt::Key_E)},
        {ToolId::Fill, IconId::Fill, QKeySequence(Qt::Key_G)},
        {ToolId::Eyedropper, IconId::Eyedropper, QKeySequence(Qt::Key_I)},
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
        // The key is a shortcut of its own, not the button's: pressed while the
        // eraser is active it goes back to the previous tool, a click does not.
        auto *shortcut = new QShortcut(e.shortcut, this);
        const bool toggles = e.tool == ToolId::Eraser;
        connect(shortcut, &QShortcut::activated, state, [state, tool = e.tool, toggles] {
            if (toggles)
                state->toggleTool(tool);
            else
                state->setActiveTool(tool);
        });
        const QString name = tools::ToolState::displayName(e.tool);
        button->setToolTip(toggles ? tr("%1 (%2; again to go back)").arg(name, e.shortcut.toString())
                                   : QStringLiteral("%1 (%2)").arg(name, e.shortcut.toString()));
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
