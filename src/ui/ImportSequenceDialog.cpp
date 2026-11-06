#include "ui/ImportSequenceDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mothion::ui {

ImportSequenceDialog::ImportSequenceDialog(int images, int currentFrame, int sceneFrames, QWidget *parent)
    : QDialog(parent)
    , m_images(images)
    , m_sceneFrames(sceneFrames)
    , m_start(new QSpinBox(this))
    , m_hold(new QSpinBox(this))
    , m_end(new QLabel(this))
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

    auto *form = new QFormLayout;
    form->addRow(new QLabel(tr("%n image(s), as one new cel with a frame for each.", nullptr, images), this));
    form->addRow(tr("Start at frame"), m_start);
    form->addRow(tr("Frames per image"), m_hold);
    form->addRow(m_end);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
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

void ImportSequenceDialog::showEnd()
{
    const int last = startFrame() + m_images * hold(); // the frame after the last one, from 0
    QString text = tr("Frames %1 to %2.").arg(startFrame() + 1).arg(last);
    if (last > m_sceneFrames)
        text += QLatin1Char(' ') + tr("The scene grows from %1 to %2 frames.").arg(m_sceneFrames).arg(last);
    m_end->setText(text);
}

} // namespace mothion::ui
