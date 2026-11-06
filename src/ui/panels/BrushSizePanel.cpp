#include "ui/panels/BrushSizePanel.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

using tools::ToolState;

namespace {

constexpr QSize kCell(40, 46);
constexpr int kDotArea = 30;  // the top of a cell, above the number
constexpr int kMaxDot = 44;   // bigger dots are drawn this size, cut by the cell

bool sameSize(double a, double b) { return std::abs(a - b) < 0.05; }

QString sizeText(double size)
{
    return QString::number(size, 'g', 4);
}

} // namespace

BrushSizePanel::BrushSizePanel(ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_grid(new BrushSizeGrid(state))
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidget(m_grid);
    scroll->setWidgetResizable(true); // the grid wraps to the width, its height follows
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false);
    m_grid->setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
}

// About three rows: the rest scrolls.
QSize BrushSizePanel::sizeHint() const
{
    return {m_grid->sizeHint().width(), 3 * kCell.height()};
}

QSize BrushSizePanel::minimumSizeHint() const
{
    return kCell;
}

BrushSizeGrid::BrushSizeGrid(ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
{
    setMouseTracking(true);
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    connect(m_state, &ToolState::activeToolChanged, this, [this] { update(); });
    connect(m_state, &ToolState::toolSettingsChanged, this, [this] { update(); });
}

const QList<double> &BrushSizeGrid::presets()
{
    static const QList<double> sizes{1,   1.5, 2,   2.5, 3,   4,   5,   6,    7,    8,    10,  12,
                                     15,  17,  20,  25,  30,  40,  50,  60,   70,   80,   100, 120,
                                     150, 170, 200, 250, 300, 400, 500, 700, 1000, 1500, 2000};
    return sizes;
}

int BrushSizeGrid::columnsFor(int width)
{
    return std::max(1, width / kCell.width());
}

int BrushSizeGrid::heightForWidth(int width) const
{
    const int count = int(presets().size());
    const int rows = (count + columnsFor(width) - 1) / columnsFor(width);
    return rows * kCell.height();
}

QSize BrushSizeGrid::sizeHint() const
{
    const int width = 6 * kCell.width();
    return {width, heightForWidth(width)};
}

QRect BrushSizeGrid::cellRect(int index) const
{
    // Cells share the spare width, so the grid fills the panel.
    const int cols = columns();
    const double cellWidth = double(width()) / cols;
    const int col = index % cols, row = index / cols;
    const int left = int(std::round(col * cellWidth)), right = int(std::round((col + 1) * cellWidth));
    return {left, row * kCell.height(), right - left, kCell.height()};
}

int BrushSizeGrid::presetAt(QPoint pos) const
{
    for (int i = 0; i < presets().size(); ++i)
        if (cellRect(i).contains(pos))
            return i;
    return -1;
}

void BrushSizeGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();
    const bool enabled = isEnabled() && ToolState::hasSize(m_state->activeTool());
    const double current = m_state->settings(m_state->activeTool()).size;
    const QColor ink = enabled ? pal.color(QPalette::Text) : pal.color(QPalette::Disabled, QPalette::Text);

    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);

    for (int i = 0; i < presets().size(); ++i) {
        const double size = presets()[i];
        const QRect cell = cellRect(i);
        const bool selected = enabled && sameSize(size, current);
        const bool hovered = enabled && i == m_hovered;
        QColor background = selected ? pal.color(QPalette::Highlight)
                          : hovered  ? pal.color(QPalette::Midlight)
                                     : pal.color(QPalette::Window);
        if (selected || hovered)
            p.fillRect(cell, background);
        p.setPen(pal.color(QPalette::Dark));
        p.setBrush(Qt::NoBrush); // just the outline: the brush may still hold the last label's backing
        p.drawRect(QRectF(cell).adjusted(0.5, 0.5, -0.5, -0.5));

        // The dot at its real size, up to what the cell can show.
        p.save();
        p.setClipRect(cell.adjusted(1, 1, -1, -1));
        const double dot = std::min(size, double(kMaxDot));
        const QPointF centre(cell.center().x() + 0.5, cell.top() + kDotArea / 2.0 + (dot > kDotArea ? 4 : 0));
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawEllipse(centre, dot / 2, dot / 2);
        p.restore();

        const QString text = sizeText(size);
        const QRect labelArea(cell.left(), cell.top() + kDotArea, cell.width(), cell.height() - kDotArea);
        const QRect label = p.fontMetrics().boundingRect(labelArea, Qt::AlignHCenter | Qt::AlignTop, text);
        // A big dot runs under the number: a backing in the cell's color keeps it readable.
        if (centre.y() + dot / 2 > label.top()) {
            background.setAlpha(200);
            p.setPen(Qt::NoPen);
            p.setBrush(background);
            p.drawRoundedRect(QRectF(label).adjusted(-3, 0, 3, 0), 3, 3);
        }
        p.setPen(ink);
        p.drawText(labelArea, Qt::AlignHCenter | Qt::AlignTop, text);
    }
}

void BrushSizeGrid::mousePressEvent(QMouseEvent *event)
{
    const int index = presetAt(event->position().toPoint());
    if (event->button() != Qt::LeftButton || index < 0 || !ToolState::hasSize(m_state->activeTool()))
        return;
    m_state->setSize(m_state->activeTool(), presets()[index]);
}

void BrushSizeGrid::mouseMoveEvent(QMouseEvent *event)
{
    const int index = presetAt(event->position().toPoint());
    if (index != m_hovered) {
        m_hovered = index;
        update();
    }
}

void BrushSizeGrid::leaveEvent(QEvent *)
{
    m_hovered = -1;
    update();
}

} // namespace mothion::ui
