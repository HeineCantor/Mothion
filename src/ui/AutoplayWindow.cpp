#include "ui/AutoplayWindow.h"

#include "document/DocumentController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

namespace {

// Paints the composed frame scaled to fit, centered on the desk color.
class FramePicture : public QWidget
{
public:
    FramePicture(const QImage *image, QWidget *parent)
        : QWidget(parent)
        , m_image(image)
    {
        setMinimumSize(160, 90);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    QSize sizeHint() const override { return {640, 360}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QColor window = palette().color(QPalette::Window);
        p.fillRect(rect(), window.lightness() < 128 ? window.lighter(135) : window.darker(115));
        if (m_image->isNull())
            return;
        const QSize fitted = m_image->size().scaled(size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, *m_image);
    }

private:
    const QImage *m_image;
};

} // namespace

const QList<double> &AutoplayWindow::speeds()
{
    static const QList<double> fps{1, 2, 4, 6, 8, 10, 12, 15, 24, 25, 30, 48, 50, 60};
    return fps;
}

AutoplayWindow::AutoplayWindow(document::DocumentController *document, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_document(document)
    , m_picture(new FramePicture(&m_renderer.image(), this))
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_position(new QLabel(this))
    , m_play(new QCheckBox(tr("Play"), this))
    , m_speed(new QComboBox(this))
{
    setWindowTitle(tr("Autoplay"));
    setObjectName(QStringLiteral("AutoplayWindow"));

    // The speeds, with the document's own among them, chosen to begin with.
    const double rate = m_document->document().frameRate().fps();
    QList<double> choices = speeds();
    if (std::none_of(choices.begin(), choices.end(), [rate](double f) { return std::abs(f - rate) < 0.01; })) {
        choices << rate;
        std::sort(choices.begin(), choices.end());
    }
    for (double f : choices)
        m_speed->addItem(tr("%1 fps").arg(f, 0, 'g', 4), f);
    for (int i = 0; i < m_speed->count(); ++i)
        if (std::abs(m_speed->itemData(i).toDouble() - rate) < 0.01)
            m_speed->setCurrentIndex(i);
    m_play->setChecked(true);
    m_position->setMinimumWidth(m_position->fontMetrics().horizontalAdvance(QStringLiteral("0000 / 0000")));
    m_position->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *track = new QHBoxLayout;
    track->addWidget(m_slider, 1);
    track->addWidget(m_position);
    auto *controls = new QHBoxLayout;
    controls->addWidget(m_play);
    controls->addStretch(1);
    controls->addWidget(new QLabel(tr("Speed"), this));
    controls->addWidget(m_speed);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_picture, 1);
    layout->addLayout(track);
    layout->addLayout(controls);

    connect(&m_timer, &QTimer::timeout, this, &AutoplayWindow::advance);
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(100);
    connect(&m_refresh, &QTimer::timeout, this, [this] {
        updateRange();
        showFrame(std::min(m_frame, m_frameCount - 1));
    });
    connect(m_play, &QCheckBox::toggled, this, &AutoplayWindow::updateTimer);
    connect(m_speed, &QComboBox::currentIndexChanged, this, &AutoplayWindow::updateTimer);
    // Dragged (or clicked, or keys): that frame, and the playback goes on from it.
    connect(m_slider, &QSlider::valueChanged, this, &AutoplayWindow::showFrame);

    using document::DocumentController;
    connect(m_document, &DocumentController::pixelsChanged, this, &AutoplayWindow::documentChanged);
    connect(m_document, &DocumentController::structureChanged, this, &AutoplayWindow::documentChanged);
    connect(m_document, &DocumentController::documentReplaced, this, &AutoplayWindow::documentChanged);

    resize(720, 500);
    updateRange();
    showFrame(0);
    updateTimer();
}

bool AutoplayWindow::isPlaying() const
{
    return m_timer.isActive();
}

double AutoplayWindow::fps() const
{
    return m_speed->currentData().toDouble();
}

void AutoplayWindow::updateTimer()
{
    m_timer.setInterval(std::max(1, int(std::lround(1000.0 / std::max(fps(), 0.1)))));
    if (m_play->isChecked() && isVisible())
        m_timer.start();
    else
        m_timer.stop();
}

void AutoplayWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateTimer();
}

void AutoplayWindow::hideEvent(QHideEvent *event)
{
    m_timer.stop(); // nothing to play to
    QWidget::hideEvent(event);
}

void AutoplayWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_play->toggle();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AutoplayWindow::updateRange()
{
    // To the last frame anything is exposed on (all of them on an empty document).
    const doc::Document &d = m_document->document();
    int last = 0;
    for (int i = 0; i < d.celCount(); ++i)
        last = std::max(last, d.celAt(i).timing().lastFrame());
    m_frameCount = std::clamp(last == 0 ? d.frameCount() : last, 1, d.frameCount());
    const QSignalBlocker block(m_slider);
    m_slider->setRange(0, m_frameCount - 1);
    m_slider->setValue(std::min(m_frame, m_frameCount - 1));
}

void AutoplayWindow::documentChanged()
{
    m_stale = true;
    if (!m_refresh.isActive())
        m_refresh.start();
}

void AutoplayWindow::advance()
{
    showFrame((m_frame + 1) % m_frameCount);
}

std::vector<doc::DrawingId> AutoplayWindow::exposedAt(int frame) const
{
    const doc::Document &d = m_document->document();
    std::vector<doc::DrawingId> shown;
    for (int i = 0; i < d.celCount(); ++i) {
        const doc::Cel &cel = d.celAt(i);
        shown.push_back(cel.properties().visible ? cel.timing().drawingAt(frame) : doc::kNoId);
    }
    return shown;
}

void AutoplayWindow::showFrame(int frame)
{
    m_frame = std::clamp(frame, 0, m_frameCount - 1);
    {
        const QSignalBlocker block(m_slider);
        m_slider->setValue(m_frame);
    }
    m_position->setText(QStringLiteral("%1 / %2").arg(m_frame + 1).arg(m_frameCount));
    std::vector<doc::DrawingId> shown = exposedAt(m_frame);
    if (m_stale || shown != m_shown) {
        m_renderer.renderAll(m_document->document(), m_frame);
        m_shown = std::move(shown);
        m_stale = false;
    }
    m_picture->update();
}

} // namespace mothion::ui
