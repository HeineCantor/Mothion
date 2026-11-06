#include "ui/VideoExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>

#include <algorithm>

namespace mothion::ui {

namespace {

constexpr int kMaxFrame = 99999; // exposures may run past the scene's end

const QString kDirKey = QStringLiteral("videoExport/directory");
const QString kQualityKey = QStringLiteral("videoExport/quality");
const QString kCompositingKey = QStringLiteral("videoExport/compositing");

} // namespace

VideoExportDialog::VideoExportDialog(int frameCount, bool hasCompositing, const QString &projectName, QWidget *parent)
    : QDialog(parent)
    , m_frameCount(frameCount)
{
    setWindowTitle(tr("Export video"));
    QSettings settings;

    const QString dir = settings.value(kDirKey, QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString();
    m_path = new QLineEdit(QDir(dir).filePath((projectName.isEmpty() ? tr("animation") : projectName) + QStringLiteral(".mp4")),
                           this);
    auto *browseButton = new QPushButton(tr("Browse…"), this);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browseButton);

    m_whole = new QRadioButton(tr("Whole scene (1–%1)").arg(frameCount), this);
    auto *custom = new QRadioButton(tr("Frames"), this);
    m_whole->setChecked(true);
    m_from = new QSpinBox(this);
    m_to = new QSpinBox(this);
    for (QSpinBox *box : {m_from, m_to})
        box->setRange(1, kMaxFrame);
    m_from->setValue(1);
    m_to->setValue(frameCount);
    m_from->setEnabled(false);
    m_to->setEnabled(false);
    auto *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(custom);
    rangeRow->addWidget(m_from);
    rangeRow->addWidget(new QLabel(tr("to"), this));
    rangeRow->addWidget(m_to);
    rangeRow->addStretch(1);
    connect(custom, &QRadioButton::toggled, m_from, &QWidget::setEnabled);
    connect(custom, &QRadioButton::toggled, m_to, &QWidget::setEnabled);
    connect(m_from, &QSpinBox::valueChanged, this, [this](int v) { m_to->setMinimum(v); });

    m_quality = new QComboBox(this);
    m_quality->addItem(tr("High"), 16);
    m_quality->addItem(tr("Standard"), 20);
    m_quality->addItem(tr("Small file"), 26);
    m_quality->setCurrentIndex(std::clamp(settings.value(kQualityKey, 1).toInt(), 0, 2));

    m_compositing = new QCheckBox(tr("Apply compositing"), this);
    m_compositing->setEnabled(hasCompositing);
    m_compositing->setChecked(hasCompositing && settings.value(kCompositingKey, true).toBool());
    if (!hasCompositing)
        m_compositing->setToolTip(tr("This project has no compositing network yet."));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseButton, &QPushButton::clicked, this, &VideoExportDialog::browse);

    auto *form = new QFormLayout(this);
    form->addRow(tr("File"), pathRow);
    form->addRow(m_whole);
    form->addRow(rangeRow);
    form->addRow(tr("Quality"), m_quality);
    form->addRow(m_compositing);
    if (io::findFfmpeg().isEmpty()) {
        auto *warning = new QLabel(tr("ffmpeg was not found: install it and make sure it is on the PATH."), this);
        warning->setWordWrap(true);
        form->addRow(warning);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
    form->addRow(buttons);
}

QString VideoExportDialog::path() const
{
    QString file = m_path->text().trimmed();
    if (QFileInfo(file).suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0)
        file += QStringLiteral(".mp4");
    return file;
}

io::VideoOptions VideoExportDialog::options() const
{
    io::VideoOptions options;
    options.crf = m_quality->currentData().toInt();
    options.compositing = m_compositing->isChecked();
    return options;
}

int VideoExportDialog::firstFrame() const
{
    return m_whole->isChecked() ? 0 : m_from->value() - 1;
}

int VideoExportDialog::lastFrame() const
{
    return m_whole->isChecked() ? m_frameCount - 1 : std::max(m_to->value(), m_from->value()) - 1;
}

void VideoExportDialog::browse()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Export video"), m_path->text(), tr("MP4 video (*.mp4)"));
    if (!file.isEmpty())
        m_path->setText(file);
}

void VideoExportDialog::accept()
{
    if (m_path->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Choose a file to write."));
        return;
    }
    const QFileInfo info(path());
    if (!info.dir().exists()) {
        QMessageBox::warning(this, windowTitle(), tr("The folder %1 does not exist.").arg(info.absolutePath()));
        return;
    }
    if (info.exists()
        && QMessageBox::question(this, windowTitle(), tr("%1 already exists. Replace it?").arg(info.fileName()))
               != QMessageBox::Yes)
        return;
    QSettings settings;
    settings.setValue(kDirKey, info.absolutePath());
    settings.setValue(kQualityKey, m_quality->currentIndex());
    if (m_compositing->isEnabled())
        settings.setValue(kCompositingKey, m_compositing->isChecked());
    QDialog::accept();
}

} // namespace mothion::ui
