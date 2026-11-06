#include "ui/panels/ToolPalettePanel.h"

#include "ui/IconFactory.h"
#include "ui/Shortcuts.h"
#include "ui/panels/BrushSizePanel.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QKeyEvent>
#include <QPainter>
#include <QShortcut>
#include <QLayout>
#include <QToolButton>
#include <QWidgetItem>

#include <algorithm>
#include <vector>

namespace mothion::ui {

using tools::ToolId;

namespace {

class GroupLine : public QWidget
{
public:
    static constexpr int kInset = 6; // px left out at each end: the line is narrower than the panel

    explicit GroupLine(QWidget *parent) : QWidget(parent) { setFixedHeight(1); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        QPainter(this).fillRect(rect().adjusted(kInset, 0, -kInset, 0), dark ? QColor(0xa5, 0xa5, 0xab) : QColor(0x55, 0x55, 0x5b));
    }
};


// Lays the buttons out in a grid whose columns follow the width it is given (as many as fit,
// at least one), the block of columns centered; a line takes a row of its own and the buttons
// after it start a new row. Asked for its preferred size it wants two columns.
class ToolGridLayout : public QLayout
{
public:
    static constexpr int kPreferredColumns = 2;
    static constexpr int kLineGap = 4; // above and below a line

    explicit ToolGridLayout(QWidget *parent) : QLayout(parent) { setContentsMargins(4, 4, 4, 4); }
    ~ToolGridLayout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }

    void addButton(QWidget *button) { add(button, false); }
    void addLine(QWidget *line) { add(line, true); }

    void addItem(QLayoutItem *item) override
    {
        m_items.push_back(item);
        m_lines.push_back(false);
    }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override
    {
        return index >= 0 && index < count() ? m_items[std::size_t(index)] : nullptr;
    }
    QLayoutItem *takeAt(int index) override
    {
        if (index < 0 || index >= count())
            return nullptr;
        QLayoutItem *item = m_items[std::size_t(index)];
        m_items.erase(m_items.begin() + index);
        m_lines.erase(m_lines.begin() + index);
        return item;
    }

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override
    {
        const int gap = spacing() < 0 ? 2 : spacing();
        const int width = contentsMargins().left() + contentsMargins().right()
                          + kPreferredColumns * cell().width() + (kPreferredColumns - 1) * gap;
        return {width, heightForWidth(width)};
    }
    QSize minimumSize() const override
    {
        return {contentsMargins().left() + contentsMargins().right() + cell().width(),
                contentsMargins().top() + contentsMargins().bottom() + cell().height()};
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }

private:
    void add(QWidget *widget, bool line)
    {
        addChildWidget(widget);
        m_items.push_back(new QWidgetItem(widget));
        m_lines.push_back(line);
    }
    // The size of a button's cell: the largest button.
    QSize cell() const
    {
        QSize size(1, 1);
        for (std::size_t i = 0; i < m_items.size(); ++i)
            if (!m_lines[i])
                size = size.expandedTo(m_items[i]->sizeHint());
        return size;
    }
    // Places the items for `rect` (when `place`) and returns the height they need.
    int arrange(const QRect &rect, bool place) const
    {
        const QMargins m = contentsMargins();
        const QSize cellSize = cell();
        const int gap = spacing() < 0 ? 2 : spacing();
        const int available = rect.width() - m.left() - m.right();
        const int columns = std::max(1, (available + gap) / (cellSize.width() + gap));
        const int block = columns * cellSize.width() + (columns - 1) * gap;
        const int left = rect.x() + m.left() + std::max(0, (available - block) / 2);
        int y = rect.y() + m.top();
        int column = 0;
        for (std::size_t i = 0; i < m_items.size(); ++i) {
            if (m_lines[i]) {
                if (column > 0) // the row in progress is closed
                    y += cellSize.height();
                y += kLineGap;
                if (place)
                    m_items[i]->setGeometry(QRect(left, y, block, 1));
                y += 1 + kLineGap;
                column = 0;
                continue;
            }
            if (column == columns) { // the row is full
                y += cellSize.height() + gap;
                column = 0;
            }
            if (place)
                m_items[i]->setGeometry(QRect(QPoint(left + column * (cellSize.width() + gap), y), cellSize));
            ++column;
        }
        if (column > 0)
            y += cellSize.height();
        return y + m.bottom() - rect.y();
    }

    std::vector<QLayoutItem *> m_items;
    std::vector<bool> m_lines;
};

}

ToolPalettePanel::ToolPalettePanel(tools::ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_group(new QButtonGroup(this))
{
    struct Entry
    {
        ToolId tool;
        IconId icon;
        bool button = true; // false: a key only
        // The key is the command's in Shortcuts, "tool.brush"...
    };
    // The groups of the palette, top to bottom, a line between them.
    const std::vector<std::vector<Entry>> groups = {
        {   // drawing
            {ToolId::Brush, IconId::Brush},
            {ToolId::Eraser, IconId::Eraser},
            {ToolId::Eyedropper, IconId::Eyedropper},
            {ToolId::Blend, IconId::Blend},
            {ToolId::Liquify, IconId::Liquify},
            {ToolId::Shape, IconId::Shape},
        },
        {   // selection and fill
            {ToolId::RectSelect, IconId::RectSelect}, // the selection tool
            {ToolId::LassoSelect, IconId::LassoSelect, false},
            {ToolId::Fill, IconId::Fill},
        },
        {   // general
            {ToolId::Hand, IconId::Hand},
            {ToolId::Zoom, IconId::Zoom},
            {ToolId::Move, IconId::Move},
            {ToolId::Puppet, IconId::Puppet},
        },
    };

    auto *layout = new ToolGridLayout(this);
    layout->setSpacing(2);
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (g > 0) {
            auto *line = new GroupLine(this);
            layout->addLine(line);
        }
        for (const Entry &e : groups[g]) {
            // The key is a shortcut of its own, not the button's: pressed while the
            // eraser is active it goes back to the previous tool, a click does not.
            const QString command = Shortcuts::toolCommand(int(e.tool));
            auto *shortcut = new QShortcut(this);
            Shortcuts::instance().bind(shortcut, command);
            shortcut->setAutoRepeat(false); // a held key selects once
            const bool toggles = e.tool == ToolId::Eraser;
            const bool selection = e.tool == ToolId::RectSelect && e.button;
            connect(shortcut, &QShortcut::activated, this,
                    [this, state, command, tool = e.tool, toggles, selection] {
                const QKeySequence now = Shortcuts::instance().key(command);
                const int key = now.isEmpty() ? 0 : int(now[0].key());
                const ToolId before = state->activeTool();
                if (toggles)
                    state->toggleTool(tool);
                else
                    state->setActiveTool(selection ? target(tool) : tool);
                m_heldKey = key;
                m_heldTool = state->activeTool();
                m_toolBeforeHold = before;
                m_held.start();
            });
            if (!e.button)
                continue;
            auto *button = new QToolButton(this);
            button->setIcon(icon(e.icon));
            button->setIconSize(QSize(24, 24));
            button->setCheckable(true);
            button->setAutoRaise(true);
            const QString name = selection ? tr("Selection") : tools::ToolState::displayName(e.tool);
            // The tip shows the key now, and follows a change of it.
            auto tip = [button, name, command, toggles] {
                const QString key = Shortcuts::instance().key(command).toString(QKeySequence::NativeText);
                button->setToolTip(key.isEmpty() ? name
                                   : toggles     ? tr("%1 (%2; again to go back)").arg(name, key)
                                                 : QStringLiteral("%1 (%2)").arg(name, key));
            };
            tip();
            connect(&Shortcuts::instance(), &Shortcuts::changed, button, [tip, command](const QString &which) {
                if (which == command)
                    tip();
            });
            m_group->addButton(button, static_cast<int>(e.tool));
            layout->addButton(button);
        }
    }

    auto *swap = new QShortcut(this);
    Shortcuts::instance().bind(swap, QStringLiteral("color.switch"));
    connect(swap, &QShortcut::activated, state, &tools::ToolState::switchColor);
    auto *smaller = new QShortcut(this);
    Shortcuts::instance().bind(smaller, QStringLiteral("brush.smaller"));
    connect(smaller, &QShortcut::activated, this, [this] { stepSize(-1); });
    auto *bigger = new QShortcut(this);
    Shortcuts::instance().bind(bigger, QStringLiteral("brush.bigger"));
    connect(bigger, &QShortcut::activated, this, [this] { stepSize(+1); });
    qApp->installEventFilter(this); // the release of a held tool key, wherever the focus is

    if (state->activeTool() == ToolId::LassoSelect)
        m_lastSelection = ToolId::LassoSelect;
    if (QAbstractButton *button = buttonFor(state->activeTool()))
        button->setChecked(true);
    connect(m_group, &QButtonGroup::idClicked, state,
            [this, state](int id) { state->setActiveTool(target(static_cast<ToolId>(id))); });
    connect(state, &tools::ToolState::activeToolChanged, this, [this](ToolId tool) {
        if (tool == ToolId::RectSelect || tool == ToolId::LassoSelect)
            m_lastSelection = tool;
        if (QAbstractButton *button = buttonFor(tool)) {
            button->setChecked(true);
        } else if (QAbstractButton *checked = m_group->checkedButton()) { // a tool with no button (transform)
            m_group->setExclusive(false);
            checked->setChecked(false);
            m_group->setExclusive(true);
        }
    });
}

QAbstractButton *ToolPalettePanel::buttonFor(ToolId tool) const
{
    return m_group->button(static_cast<int>(tool == ToolId::LassoSelect ? ToolId::RectSelect : tool));
}

ToolId ToolPalettePanel::target(ToolId tool) const
{
    return tool == ToolId::RectSelect ? m_lastSelection : tool;
}

void ToolPalettePanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // The rows the buttons make depend on the width: the height they need becomes the minimum,
    // so that the panel scrolls (see PanelDock) instead of cutting the last row off.
    if (layout()) {
        const int needed = layout()->heightForWidth(width());
        if (needed != minimumHeight())
            setMinimumHeight(needed);
    }
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
    if (!tools::ToolState::hasSize(tool))
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
