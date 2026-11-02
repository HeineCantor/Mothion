#include "ui/panels/ToolPalettePanel.h"

#include "ui/IconFactory.h"
#include "ui/panels/BrushSizePanel.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QKeyEvent>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace mothion::ui {

using tools::ToolId;

ToolPalettePanel::ToolPalettePanel(tools::ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
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
        shortcut->setAutoRepeat(false); // a held key selects once
        const bool toggles = e.tool == ToolId::Eraser;
        connect(shortcut, &QShortcut::activated, this, [this, state, key = e.shortcut[0].key(), tool = e.tool, toggles] {
            const ToolId before = state->activeTool();
            if (toggles)
                state->toggleTool(tool);
            else
                state->setActiveTool(tool);
            m_heldKey = key;
            m_heldTool = state->activeTool();
            m_toolBeforeHold = before;
            m_held.start();
        });
        const QString name = tools::ToolState::displayName(e.tool);
        button->setToolTip(toggles ? tr("%1 (%2; again to go back)").arg(name, e.shortcut.toString())
                                   : QStringLiteral("%1 (%2)").arg(name, e.shortcut.toString()));
        m_group->addButton(button, static_cast<int>(e.tool));
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }
    layout->addStretch(1);

    auto *swap = new QShortcut(QKeySequence(Qt::Key_X), this);
    connect(swap, &QShortcut::activated, state, &tools::ToolState::swapColors);
    auto *smaller = new QShortcut(QKeySequence(Qt::Key_BracketLeft), this);
    connect(smaller, &QShortcut::activated, this, [this] { stepSize(-1); });
    auto *bigger = new QShortcut(QKeySequence(Qt::Key_BracketRight), this);
    connect(bigger, &QShortcut::activated, this, [this] { stepSize(+1); });
    qApp->installEventFilter(this); // the release of a held tool key, wherever the focus is

    m_group->button(static_cast<int>(state->activeTool()))->setChecked(true);
    connect(m_group, &QButtonGroup::idClicked, state,
            [state](int id) { state->setActiveTool(static_cast<ToolId>(id)); });
    connect(state, &tools::ToolState::activeToolChanged, this,
            [this](ToolId tool) { m_group->button(static_cast<int>(tool))->setChecked(true); });
}

bool ToolPalettePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease && m_heldKey != 0) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == m_heldKey && !key->isAutoRepeat()) {
            m_heldKey = 0;
            // Held, and still on the tool it gave: back to the one before.
            if (m_held.elapsed() >= kHoldMs && m_state->activeTool() == m_heldTool)
                m_state->setActiveTool(m_toolBeforeHold);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ToolPalettePanel::stepSize(int direction)
{
    const ToolId tool = m_state->activeTool();
    if (!tools::ToolState::isPaintTool(tool))
        return;
    const double size = m_state->settings(tool).size;
    const QList<double> &presets = BrushSizeGrid::presets();
    double next = size;
    if (direction > 0) {
        const auto it = std::upper_bound(presets.begin(), presets.end(), size + 0.05);
        if (it != presets.end())
            next = *it;
    } else {
        const auto it = std::lower_bound(presets.begin(), presets.end(), size - 0.05);
        if (it != presets.begin())
            next = *(it - 1);
    }
    m_state->setSize(tool, next);
}

} // namespace mothion::ui
