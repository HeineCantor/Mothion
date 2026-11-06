#include "ui/panels/ColorSetPanel.h"

#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>

namespace mothion::ui {

namespace {

constexpr int kCell = 22;       // the width a swatch asks for; they share the spare width
constexpr int kDragDistance = 6; // before a press becomes a drag
constexpr int kHoldMs = 500;     // a swatch held this long offers its menu

QString hexOf(const QColor &c)
{
    return c.alpha() == 255 ? c.name().toUpper() : c.name(QColor::HexArgb).toUpper();
}

} // namespace

// --- the swatches ----------------------------------------------------------------------------

ColorSwatchGrid::ColorSwatchGrid(tools::ToolState *state, tools::ColorSetLibrary *library, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_library(library)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    m_hold.setSingleShot(true);
    m_hold.setInterval(kHoldMs);
    connect(&m_hold, &QTimer::timeout, this, [this] {
        if (m_pressed >= 0 && !m_dragging) {
            m_menuShown = true;
            showMenu(m_pressed, mapToGlobal(cellRect(m_pressed).center()));
        }
    });
    auto changed = [this] {
        updateGeometry();
        update();
    };
    connect(m_library, &tools::ColorSetLibrary::activeChanged, this, changed);
    connect(m_library, &tools::ColorSetLibrary::setsChanged, this, changed);
    connect(m_library, &tools::ColorSetLibrary::setChanged, this, changed);
    connect(m_state, &tools::ToolState::colorChanged, this, [this] { update(); });
}

int ColorSwatchGrid::columns() const
{
    return std::max(1, width() / kCell);
}

int ColorSwatchGrid::heightForWidth(int width) const
{
    const tools::ColorSet *set = current();
    const int count = set ? int(set->swatches.size()) : 0;
    const int cols = std::max(1, width / kCell);
    return std::max(1, (count + cols - 1) / cols) * kCell;
}

QSize ColorSwatchGrid::sizeHint() const
{
    const int width = 8 * kCell;
    return {width, heightForWidth(width)};
}

QRect ColorSwatchGrid::cellRect(int index) const
{
    const int cols = columns();
    const double cellWidth = double(width()) / cols;
    const int col = index % cols, row = index / cols;
    const int left = int(col * cellWidth), right = int((col + 1) * cellWidth);
    return {left, row * kCell, right - left, kCell};
}

int ColorSwatchGrid::swatchAt(QPoint pos) const
{
    const tools::ColorSet *set = current();
    if (!set)
        return -1;
    for (int i = 0; i < set->swatches.size(); ++i)
        if (cellRect(i).contains(pos))
            return i;
    return -1;
}

// Where a dragged swatch would land: the cell under the pointer, or the last one when it is past the end.
int ColorSwatchGrid::dropIndexAt(QPoint pos) const
{
    const tools::ColorSet *set = current();
    if (!set || set->swatches.isEmpty())
        return -1;
    const int at = swatchAt(pos);
    if (at >= 0)
        return at;
    return pos.y() >= cellRect(int(set->swatches.size()) - 1).top() ? int(set->swatches.size()) - 1 : -1;
}

void ColorSwatchGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const tools::ColorSet *set = current();
    if (!set)
        return;
    const QColor now = m_state->color();
    for (int i = 0; i < set->swatches.size(); ++i) {
        const QRect cell = cellRect(i).adjusted(0, 0, -1, -1);
        const QColor c = set->swatches[i].color;
        if (c.alpha() < 255) { // over a checkerboard, to show it is see-through
            p.fillRect(cell, Qt::white);
            p.fillRect(QRect(cell.topLeft(), cell.size() / 2), QColor(200, 200, 200));
            p.fillRect(QRect(cell.center(), cell.size() / 2), QColor(200, 200, 200));
        }
        p.fillRect(cell, c);
        if (c.rgba() == now.rgba() || i == m_hovered) {
            p.setPen(QPen(palette().color(QPalette::Highlight), 2));
            p.drawRect(cell.adjusted(1, 1, -1, -1));
        }
    }
    if (m_dragging && m_drop >= 0) { // where the dragged swatch will be
        const QRect cell = cellRect(m_drop);
        p.setPen(QPen(palette().color(QPalette::Text), 2));
        const int x = m_drop > m_pressed ? cell.right() : cell.left();
        p.drawLine(x, cell.top(), x, cell.bottom());
    }
}

bool ColorSwatchGrid::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const tools::ColorSet *set = current();
        const int at = swatchAt(help->pos());
        if (set && at >= 0) {
            const tools::ColorSwatch &s = set->swatches[at];
            QToolTip::showText(help->globalPos(), s.name.isEmpty() ? hexOf(s.color) : QStringLiteral("%1  %2").arg(s.name, hexOf(s.color)), this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

void ColorSwatchGrid::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_pressed = swatchAt(event->position().toPoint());
    m_pressPos = event->position().toPoint();
    m_dragging = m_menuShown = false;
    m_drop = -1;
    const tools::ColorSet *set = current();
    if (m_pressed >= 0 && set && !set->builtIn)
        m_hold.start(); // the built-in set has no menu to hold for
}

void ColorSwatchGrid::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const int over = swatchAt(pos);
    if (over != m_hovered) {
        m_hovered = over;
        update();
    }
    if (m_pressed < 0 || m_menuShown)
        return;
    const tools::ColorSet *set = current();
    if (!m_dragging && (pos - m_pressPos).manhattanLength() > kDragDistance && set && !set->builtIn) {
        m_dragging = true;
        m_hold.stop();
    }
    if (m_dragging) {
        const int drop = dropIndexAt(pos);
        if (drop != m_drop) {
            m_drop = drop;
            update();
        }
    }
}

void ColorSwatchGrid::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_hold.stop();
    const tools::ColorSet *set = current();
    if (m_dragging) {
        if (set && m_pressed >= 0 && m_drop >= 0 && m_drop != m_pressed)
            m_library->moveColor(set->id, m_pressed, m_drop);
    } else if (!m_menuShown && set && m_pressed >= 0 && m_pressed == swatchAt(event->position().toPoint())) {
        m_state->setColor(set->swatches[m_pressed].color);
    }
    m_pressed = m_drop = -1;
    m_dragging = m_menuShown = false;
    update();
}

void ColorSwatchGrid::leaveEvent(QEvent *event)
{
    m_hovered = -1;
    update();
    QWidget::leaveEvent(event);
}

void ColorSwatchGrid::contextMenuEvent(QContextMenuEvent *event)
{
    const tools::ColorSet *set = current();
    const int at = swatchAt(event->pos());
    if (set && !set->builtIn && at >= 0)
        showMenu(at, event->globalPos());
}

void ColorSwatchGrid::editColor(int index)
{
    const tools::ColorSet *set = current();
    if (!set || set->builtIn || index < 0 || index >= set->swatches.size())
        return;
    const QString id = set->id;
    const tools::ColorSwatch swatch = set->swatches[index];
    // Qt's own dialog, not the system's (which can hang over a window like this one), and not blocking.
    auto *dialog = new QColorDialog(swatch.color, window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setOptions(QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
    dialog->setWindowTitle(swatch.name.isEmpty() ? tr("Edit color") : tr("Edit color “%1”").arg(swatch.name));
    connect(dialog, &QColorDialog::colorSelected, this, [this, id, index](const QColor &color) {
        m_library->setColor(id, index, color);
    });
    dialog->open();
}

void ColorSwatchGrid::showMenu(int index, const QPoint &globalPos)
{
    const tools::ColorSet *set = current();
    if (!set || set->builtIn || index < 0 || index >= set->swatches.size())
        return;
    const QString id = set->id;
    QMenu menu(this);
    menu.addAction(tr("Use this color"), this, [this, id, index] {
        if (const tools::ColorSet *s = m_library->set(id); s && index < s->swatches.size())
            m_state->setColor(s->swatches[index].color);
    });
    menu.addAction(tr("Rename…"), this, [this, id, index] {
        const tools::ColorSet *s = m_library->set(id);
        if (!s || index >= s->swatches.size())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename color"), tr("Name:"), QLineEdit::Normal,
                                                   s->swatches[index].name, &ok);
        if (ok)
            m_library->setColorName(id, index, name);
    });
    menu.addAction(tr("Edit color…"), this, [this, index] { editColor(index); });
    menu.addAction(tr("Delete"), this, [this, id, index] { m_library->removeColor(id, index); });
    menu.exec(globalPos);
}

// --- the dock --------------------------------------------------------------------------------

ColorSetPanel::ColorSetPanel(tools::ToolState *state, tools::ColorSetLibrary *library, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_library(library)
    , m_sets(new QComboBox(this))
    , m_add(new QToolButton(this))
    , m_menu(new QToolButton(this))
    , m_grid(new ColorSwatchGrid(state, library))
{
    m_sets->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_sets->setMinimumContentsLength(6);
    m_sets->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_add->setText(QStringLiteral("+"));
    m_add->setToolTip(tr("Add the current color to the set"));
    m_add->setMinimumSize(30, 28);
    m_menu->setText(QStringLiteral("⋯"));
    m_menu->setToolTip(tr("Color sets"));
    m_menu->setPopupMode(QToolButton::InstantPopup);
    m_menu->setMinimumSize(30, 28);
    auto *menu = new QMenu(m_menu);
    menu->addAction(tr("New set…"), this, &ColorSetPanel::newSet);
    menu->addAction(tr("Duplicate set"), this, [this] {
        if (const tools::ColorSet *set = m_grid->current()) {
            const QString id = m_library->duplicate(set->id);
            if (!id.isEmpty())
                m_library->setActive(id);
        }
    });
    m_rename = menu->addAction(tr("Rename set…"), this, &ColorSetPanel::renameSet);
    m_delete = menu->addAction(tr("Delete set"), this, &ColorSetPanel::deleteSet);
    m_menu->setMenu(menu);

    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(4);
    top->addWidget(m_sets, 1);
    top->addWidget(m_add);
    top->addWidget(m_menu);

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(m_grid);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    m_grid->setAutoFillBackground(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(top);
    layout->addWidget(scroll, 1);

    connect(m_sets, &QComboBox::activated, this, [this](int row) { m_library->setActive(m_sets->itemData(row).toString()); });
    connect(m_add, &QToolButton::clicked, this, [this] {
        if (const tools::ColorSet *set = m_grid->current())
            m_library->addColor(set->id, m_state->color());
    });
    connect(m_library, &tools::ColorSetLibrary::setsChanged, this, [this] { fillList(); });
    connect(m_library, &tools::ColorSetLibrary::activeChanged, this, [this] { fillList(); });
    fillList();
}

void ColorSetPanel::fillList()
{
    const QSignalBlocker block(m_sets);
    m_sets->clear();
    int active = 0;
    for (const tools::ColorSet &set : m_library->sets()) {
        if (set.id == m_library->activeId())
            active = m_sets->count();
        m_sets->addItem(set.name, set.id);
    }
    m_sets->setCurrentIndex(active);
    syncButtons();
}

void ColorSetPanel::syncButtons()
{
    const tools::ColorSet *set = m_grid->current();
    const bool editable = set && !set->builtIn;
    m_add->setEnabled(editable);
    m_add->setToolTip(editable ? tr("Add the current color to the set")
                               : tr("The Default set cannot be changed: duplicate it to make your own"));
    m_rename->setEnabled(editable);
    m_delete->setEnabled(editable);
}

void ColorSetPanel::newSet()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New color set"), tr("Name:"), QLineEdit::Normal, tr("New set"), &ok);
    if (!ok)
        return;
    const QString id = m_library->create(name);
    if (!id.isEmpty())
        m_library->setActive(id);
}

void ColorSetPanel::renameSet()
{
    const tools::ColorSet *set = m_grid->current();
    if (!set || set->builtIn)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename color set"), tr("Name:"), QLineEdit::Normal, set->name, &ok);
    if (ok)
        m_library->rename(set->id, name);
}

void ColorSetPanel::deleteSet()
{
    const tools::ColorSet *set = m_grid->current();
    if (!set || set->builtIn)
        return;
    if (QMessageBox::question(this, tr("Delete color set"), tr("Delete the set “%1”?").arg(set->name)) == QMessageBox::Yes)
        m_library->remove(set->id);
}

} // namespace mothion::ui
