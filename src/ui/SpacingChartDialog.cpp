#include "ui/SpacingChartDialog.h"

#include "core/document/CelMotion.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

namespace {
constexpr int kWidth = 640, kHeight = 220;
constexpr double kStart = 120, kEnd = kWidth - kStart; // where A and B are
constexpr double kLine = kHeight / 2.0;
constexpr double kOnEnd = 4; // a tick this close to A or B is that end
} // namespace

SpacingChartCanvas::SpacingChartCanvas(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("spacingChart"));
    setFixedSize(kWidth, kHeight);
    setCursor(Qt::CrossCursor);
}

QPointF SpacingChartCanvas::pointAt(double share) const
{
    return {kStart + share * (kEnd - kStart), kLine};
}

std::optional<double> SpacingChartCanvas::shareOf(const Stroke &stroke) const
{
    std::optional<double> x;
    for (std::size_t i = 1; i < stroke.size() && !x; ++i) {
        const QPointF a = stroke[i - 1], b = stroke[i];
        if ((a.y() - kLine) * (b.y() - kLine) <= 0 && a.y() != b.y())
            x = a.x() + (b.x() - a.x()) * (kLine - a.y()) / (b.y() - a.y());
    }
    if (!x) {
        double sum = 0;
        for (const QPointF &p : stroke)
            sum += p.x();
        x = sum / double(stroke.size());
    }
    if (std::abs(*x - kStart) <= kOnEnd || std::abs(*x - kEnd) <= kOnEnd)
        return std::nullopt;
    return (*x - kStart) / (kEnd - kStart);
}

std::vector<double> SpacingChartCanvas::shares() const
{
    std::vector<double> out;
    for (const Stroke &stroke : m_strokes)
        if (const auto share = shareOf(stroke))
            out.push_back(*share);
    std::sort(out.begin(), out.end());
    return out;
}

void SpacingChartCanvas::undo()
{
    if (m_strokes.empty())
        return;
    m_strokes.pop_back();
    m_drawing = false;
    update();
    emit changed();
}

void SpacingChartCanvas::clear()
{
    m_strokes.clear();
    m_drawing = false;
    update();
    emit changed();
}

void SpacingChartCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white); // paper, whatever the theme
    const QColor ink(30, 30, 30), faint(170, 170, 170), mark(220, 80, 40);
    // The line from A to B, dashed past them.
    p.setPen(QPen(faint, 1.0, Qt::DashLine));
    p.drawLine(QPointF(8, kLine), QPointF(kStart, kLine));
    p.drawLine(QPointF(kEnd, kLine), QPointF(kWidth - 8, kLine));
    p.setPen(QPen(ink, 2.0));
    p.drawLine(pointAt(0), pointAt(1));
    for (const auto &[share, name] : {std::pair{0.0, QStringLiteral("A")}, {1.0, QStringLiteral("B")}}) {
        const QPointF at = pointAt(share);
        p.drawLine(at - QPointF(0, 28), at + QPointF(0, 28));
        p.drawText(QRectF(at.x() - 20, at.y() - 52, 40, 20), Qt::AlignCenter, name);
    }
    p.setBrush(Qt::NoBrush);
    for (const Stroke &stroke : m_strokes) {
        p.setPen(QPen(shareOf(stroke) ? ink : faint, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (stroke.size() == 1)
            p.drawPoint(stroke.front());
        else
            p.drawPolyline(stroke.data(), int(stroke.size()));
    }
    // Where each tick counts, numbered in order.
    int number = 0;
    for (const double share : shares()) {
        const QPointF at = pointAt(share);
        p.setPen(Qt::NoPen);
        p.setBrush(mark);
        p.drawEllipse(at, 3.5, 3.5);
        p.setPen(mark);
        // Every other one lower, so that close ticks stay readable.
        p.drawText(QRectF(at.x() - 15, kLine + (number % 2 ? 54 : 36), 30, 16), Qt::AlignCenter, QString::number(number + 1));
        ++number;
    }
}

void SpacingChartCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_strokes.push_back({event->position()});
    m_drawing = true;
    update();
}

void SpacingChartCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_drawing)
        return;
    m_strokes.back().push_back(event->position());
    update();
}

void SpacingChartCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_drawing || event->button() != Qt::LeftButton)
        return;
    if (m_strokes.back().back() != event->position())
        m_strokes.back().push_back(event->position());
    m_drawing = false;
    update();
    emit changed();
}

SpacingChartDialog::SpacingChartDialog(int from, int to, int between, int block, QWidget *parent)
    : QDialog(parent)
    , m_from(from)
    , m_to(to)
    , m_between(between)
    , m_block(block)
{
    setWindowTitle(tr("Spacing chart"));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("A is the keyframe on frame %1, B the one on frame %2. Draw a tick for each "
                                    "inbetween.").arg(from + 1).arg(to + 1), this));
    m_canvas = new SpacingChartCanvas(this);
    layout->addWidget(m_canvas);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    auto *row = new QHBoxLayout;
    m_undo = new QPushButton(tr("Undo tick"), this);
    m_clear = new QPushButton(tr("Clear"), this);
    for (QPushButton *button : {m_undo, m_clear}) {
        button->setAutoDefault(false);
        row->addWidget(button);
    }
    row->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    row->addWidget(buttons);
    layout->addLayout(row);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_undo, &QPushButton::clicked, m_canvas, &SpacingChartCanvas::undo);
    connect(m_clear, &QPushButton::clicked, m_canvas, &SpacingChartCanvas::clear);
    connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, m_canvas, &SpacingChartCanvas::undo);
    connect(m_canvas, &SpacingChartCanvas::changed, this, &SpacingChartDialog::refresh);
    refresh();
}

void SpacingChartDialog::refresh()
{
    const int count = int(m_canvas->shares().size());
    m_ok->setEnabled(count > 0);
    m_undo->setEnabled(m_canvas->hasStrokes());
    m_clear->setEnabled(m_canvas->hasStrokes());
    QStringList lines;
    if (count == 0) {
        lines << tr("No ticks yet.");
    } else {
        lines << (count == 1 ? tr("New keyframe on frame %1.").arg(m_from + 2)
                             : tr("New keyframes on frames %1 to %2.").arg(m_from + 2).arg(m_from + count + 1));
        const int end = doc::CelMotion::spacingEnd(m_from, count, m_block);
        if (end != m_to)
            lines << tr("B moves to frame %1, with the keyframes after it.").arg(end + 1);
        if (m_block > m_from + count)
            lines << tr("B stays after frame %1: another channel has a keyframe there.").arg(m_block + 1);
    }
    if (m_between > 0)
        lines << tr("%n keyframe(s) between A and B will be replaced.", nullptr, m_between);
    m_summary->setText(lines.join(QLatin1Char('\n')));
}

} // namespace mothion::ui
