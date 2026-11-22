#include "ui/ImportSequenceDialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

ImportSequenceDialog::ImportSequenceDialog(int images, int currentFrame, int sceneFrames, QWidget *parent)
    : QDialog(parent)
    , m_images(images)
    , m_sceneFrames(sceneFrames)
    , m_start(new QSpinBox(this))
    , m_hold(new QSpinBox(this))
    , m_end(new QLabel(this))
    , m_count(new QLabel(this))
    , m_form(new QFormLayout)
{
    setObjectName(QStringLiteral("ImportSequenceDialog"));
    setWindowTitle(tr("Import image sequence"));
    m_start->setObjectName(QStringLiteral("start"));
    m_start->setRange(1, 99999);
    m_start->setValue(currentFrame + 1);
    m_hold->setObjectName(QStringLiteral("hold"));
    m_hold->setRange(1, 48);
    m_hold->setValue(1);
    m_hold->setToolTip(tr("How many frames each image is shown for: 2 for animation on twos"));
    m_end->setObjectName(QStringLiteral("end"));
    m_end->setWordWrap(true);
    m_end->setEnabled(false);
    m_count->setObjectName(QStringLiteral("count"));

    m_form->addRow(m_count);
    m_form->addRow(tr("Start at frame"), m_start);
    m_form->addRow(tr("Frames per image"), m_hold);
    m_form->addRow(m_end);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(m_form);
    layout->addWidget(buttons);

    connect(m_start, &QSpinBox::valueChanged, this, &ImportSequenceDialog::showEnd);
    connect(m_hold, &QSpinBox::valueChanged, this, &ImportSequenceDialog::showEnd);
    showEnd();
}

int ImportSequenceDialog::startFrame() const
{
    return m_start->value() - 1;
}

int ImportSequenceDialog::hold() const
{
    return m_hold->value();
}

void ImportSequenceDialog::offerFrameRates(double sceneFps, double videoFps, double duration)
{
    m_sceneFps = sceneFps;
    m_videoFps = videoFps;
    m_duration = duration;
    m_sceneRate = new QRadioButton(this);
    m_sceneRate->setObjectName(QStringLiteral("sceneRate"));
    m_sceneRate->setToolTip(tr("Frames dropped or held so that the video keeps its timing in the scene"));
    m_everyFrame = new QRadioButton(this);
    m_everyFrame->setObjectName(QStringLiteral("everyFrame"));
    m_everyFrame->setToolTip(tr("Each frame of the video a frame of the scene: it plays slower or faster if the "
                                "rates differ"));
    m_everyFrame->setVisible(videoFps > 0);
    m_customRate = new QRadioButton(this);
    m_customRate->setObjectName(QStringLiteral("customRate"));
    m_custom = new QDoubleSpinBox(this);
    m_custom->setObjectName(QStringLiteral("customFps"));
    m_custom->setRange(0.1, 240.0);
    m_custom->setDecimals(3);
    m_custom->setSuffix(tr(" fps"));
    m_custom->setValue(videoFps > 0 ? std::min(videoFps, 240.0) : sceneFps);
    auto *group = new QButtonGroup(this);
    for (QRadioButton *b : {m_sceneRate, m_everyFrame, m_customRate})
        group->addButton(b);
    m_sceneRate->setChecked(true);
    auto *custom = new QHBoxLayout;
    custom->addWidget(m_customRate);
    custom->addWidget(m_custom);
    custom->addStretch();
    auto *rates = new QVBoxLayout;
    rates->addWidget(m_sceneRate);
    rates->addWidget(m_everyFrame);
    rates->addLayout(custom);
    m_form->insertRow(0, tr("Frame rate"), rates);
    connect(group, &QButtonGroup::buttonToggled, this, [this] { showRates(); });
    connect(m_custom, &QDoubleSpinBox::valueChanged, this, [this] {
        m_customRate->setChecked(true);
        showRates();
    });
    showRates();
}

double ImportSequenceDialog::frameRate() const
{
    if (!m_sceneRate || m_everyFrame->isChecked())
        return 0.0;
    return m_customRate->isChecked() ? m_custom->value() : m_sceneFps;
}

int ImportSequenceDialog::imagesAt(double fps) const
{
    const double rate = fps > 0 ? fps : m_videoFps;
    return std::max(0, int(std::lround(m_duration * rate)));
}

void ImportSequenceDialog::showRates()
{
    const QString fps = QStringLiteral("%1 fps");
    m_sceneRate->setText(tr("The scene's, %1: %n frame(s)", nullptr, imagesAt(m_sceneFps))
                             .arg(fps.arg(m_sceneFps, 0, 'g', 6)));
    m_everyFrame->setText(tr("Every frame of the video, %1: %n frame(s)", nullptr, imagesAt(0))
                              .arg(fps.arg(m_videoFps, 0, 'g', 6)));
    m_customRate->setText(tr("Custom: %n frame(s)", nullptr, imagesAt(m_custom->value())));
    m_images = imagesAt(frameRate());
    showEnd();
}

void ImportSequenceDialog::showEnd()
{
    m_count->setText(m_sceneRate ? tr("%n picture(s), as one new cel with a frame for each.", nullptr, m_images)
                                 : tr("%n image(s), as one new cel with a frame for each.", nullptr, m_images));
    const int last = startFrame() + m_images * hold(); // the frame after the last one, from 0
    QString text = tr("Frames %1 to %2.").arg(startFrame() + 1).arg(last);
    if (last > m_sceneFrames)
        text += QLatin1Char(' ') + tr("The scene grows from %1 to %2 frames.").arg(m_sceneFrames).arg(last);
    m_end->setText(text);
}

} // namespace mothion::ui
