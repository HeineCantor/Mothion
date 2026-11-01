#include "canvas/DebugOverlay.h"

#include "input/TabletBackend.h"

#include <QGuiApplication>
#include <QPainter>

namespace mothion::canvas {

namespace {

QString toolName(InputTool tool)
{
    switch (tool) {
    case InputTool::Pen: return QStringLiteral("pen");
    case InputTool::Eraser: return QStringLiteral("eraser");
    case InputTool::Mouse: return QStringLiteral("mouse");
    }
    return {};
}

QString phaseName(SamplePhase phase)
{
    switch (phase) {
    case SamplePhase::Hover: return QStringLiteral("Hover");
    case SamplePhase::Press: return QStringLiteral("Press");
    case SamplePhase::Move: return QStringLiteral("Move");
    case SamplePhase::Release: return QStringLiteral("Release");
    }
    return {};
}

QString buttonNames(std::uint8_t b)
{
    QStringList names;
    if (b & PenButtonTip)
        names << QStringLiteral("tip/left");
    if (b & PenButtonMiddle)
        names << QStringLiteral("middle");
    if (b & PenButtonRight)
        names << QStringLiteral("right");
    return names.isEmpty() ? QStringLiteral("–") : names.join(QStringLiteral(" + "));
}

QString yesNo(bool v) { return v ? QStringLiteral("yes") : QStringLiteral("no"); }

} // namespace

DebugOverlay::DebugOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents); // also lets tablet events through
    setAttribute(Qt::WA_NoSystemBackground);
    m_clock.start();
    m_refresh.setInterval(33); // ~30 fps is plenty for reading numbers
    connect(&m_refresh, &QTimer::timeout, this, &DebugOverlay::refresh);
}

void DebugOverlay::addSample(const PenSample &sample)
{
    m_rate.add(seconds());
    ++m_totalSamples;
    if (sample.phase == SamplePhase::Press)
        m_strokeSamples = 0;
    if (sample.tipDown() || sample.phase == SamplePhase::Release)
        ++m_strokeSamples;
    m_last = sample;
}

void DebugOverlay::setDevice(const InputDeviceInfo &device)
{
    m_device = device;
}

void DebugOverlay::setProximity(const ProximityEvent &event)
{
    m_inProximity = event.entering;
    m_proximityKnown = true;
}

void DebugOverlay::showEvent(QShowEvent *event)
{
    m_backend = input::backendDescription(); // may change at runtime (Wintab switch)
    m_refresh.start();
    refresh();
    QWidget::showEvent(event);
}

void DebugOverlay::hideEvent(QHideEvent *event)
{
    m_refresh.stop();
    QWidget::hideEvent(event);
}

QStringList DebugOverlay::lines()
{
    const PenSample &s = m_last;
    const double hz = m_rate.rateHz(seconds());
    const QString device = m_device.name.empty()
        ? QStringLiteral("(no events yet)")
        : QStringLiteral("%1  [%2, %3]").arg(QString::fromStdString(m_device.name),
                                             QString::fromStdString(m_device.type), toolName(m_device.tool));
    return {
        QStringLiteral("DEBUG INPUT (F12)"),
        QStringLiteral("Platform: %1  ·  Backend: %2").arg(QGuiApplication::platformName(), m_backend),
        QStringLiteral("Device: %1").arg(device),
        QStringLiteral("Capabilities: pressure %1 · tilt %2 · rotation %3")
            .arg(yesNo(m_device.hasPressure), yesNo(m_device.hasTilt), yesNo(m_device.hasRotation)),
        QStringLiteral("Proximity: %1").arg(m_proximityKnown ? (m_inProximity ? QStringLiteral("in")
                                                                               : QStringLiteral("out"))
                                                              : QStringLiteral("no events")),
        QStringLiteral("Rate: %1 Hz  ·  samples: %2 (stroke: %3)")
            .arg(hz, 0, 'f', 0).arg(m_totalSamples).arg(m_strokeSamples),
        QString(),
        QStringLiteral("Tool: %1  ·  phase: %2  ·  buttons: %3")
            .arg(toolName(s.tool), phaseName(s.phase), buttonNames(s.buttons)),
        QStringLiteral("Position: %1, %2").arg(s.x, 0, 'f', 2).arg(s.y, 0, 'f', 2),
        QStringLiteral("Pressure: %1").arg(s.pressure, 0, 'f', 3),
        QStringLiteral("Tilt: X %1°  Y %2°").arg(s.tiltX, 0, 'f', 1).arg(s.tiltY, 0, 'f', 1),
        QStringLiteral("Rotation: %1°").arg(s.rotation, 0, 'f', 1),
        QStringLiteral("Timestamp: %1 s").arg(s.t, 0, 'f', 3),
    };
}

namespace {
constexpr int kPad = 8;
}

QFont DebugOverlay::font() const
{
    QFont f(QStringLiteral("monospace"));
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(9);
    return f;
}

void DebugOverlay::refresh()
{
    m_text = lines();
    const QFontMetrics fm(font());
    int width = 0;
    for (const QString &line : m_text)
        width = std::max(width, fm.horizontalAdvance(line));
    // Only grow, so the box does not jitter as numbers change width.
    const QSize wanted(std::max(this->width(), width + 2 * kPad),
                       int(m_text.size()) * fm.height() + 2 * kPad);
    if (wanted != size())
        resize(wanted);
    update();
}

void DebugOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QFont f = font();
    const QFontMetrics fm(f);
    p.setFont(f);
    p.fillRect(rect(), QColor(0, 0, 0, 190));
    p.setPen(QColor(0xe0, 0xe0, 0xe0));
    int y = kPad + fm.ascent();
    for (const QString &line : std::as_const(m_text)) {
        p.drawText(kPad, y, line);
        y += fm.height();
    }

    // Pressure bar along the bottom edge.
    const double w = (width() - 2 * kPad) * double(m_last.pressure);
    p.fillRect(QRectF(kPad, height() - 4, w, 2), QColor(0xff, 0xb0, 0x30));
}

} // namespace mothion::canvas
