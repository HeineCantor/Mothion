#include "ui/ExportDialog.h"

#include <QButtonGroup>
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

enum Range { CurrentFrame, WholeScene, CustomRange };

constexpr int kMaxFrame = 99999; // exposures may run past the scene's end

const QString kDirKey = QStringLiteral("export/directory");
const QString kFormatKey = QStringLiteral("export/format");
const QString kTransparentKey = QStringLiteral("export/transparent");
const QString kCompositingKey = QStringLiteral("export/compositing");
const QString kQualityKey = QStringLiteral("export/quality");
const QString kRangeKey = QStringLiteral("export/range");

io::ImageFormat formatAt(const QComboBox *box) { return io::ImageFormat(box->currentData().toInt()); }

} // namespace

ExportDialog::ExportDialog(int currentFrame, int frameCount, const QString &projectName, bool hasCompositing,
                           QWidget *parent)
    : QDialog(parent)
    , m_currentFrame(currentFrame)
    , m_frameCount(frameCount)
{
    setWindowTitle(tr("Export frames"));
    QSettings settings;

    m_format = new QComboBox(this);
    m_format->addItem(tr("PNG"), int(io::ImageFormat::Png));
    m_format->addItem(tr("JPEG"), int(io::ImageFormat::Jpeg));
    m_format->setCurrentIndex(std::clamp(settings.value(kFormatKey, 0).toInt(), 0, 1));

    const QString dir = settings.value(kDirKey, QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                            .toString();
    const QString base = projectName.isEmpty() ? tr("frame") : projectName;
    m_path = new QLineEdit(QDir(dir).filePath(base + QLatin1Char('.') + io::suffixOf(formatAt(m_format))), this);
    auto *browse = new QPushButton(tr("Browse…"), this);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);

    m_transparent = new QCheckBox(tr("Transparent background"), this);
    m_transparent->setChecked(settings.value(kTransparentKey, false).toBool());
    m_compositing = new QCheckBox(tr("Apply compositing"), this);
    m_compositing->setEnabled(hasCompositing);
    m_compositing->setChecked(hasCompositing && settings.value(kCompositingKey, true).toBool());
    if (!hasCompositing)
        m_compositing->setToolTip(tr("This project has no compositing network yet."));
    m_quality = new QSpinBox(this);
    m_quality->setRange(1, 100);
    m_quality->setValue(settings.value(kQualityKey, 90).toInt());

    m_range = new QButtonGroup(this);
    auto *current = new QRadioButton(tr("Current frame (%1)").arg(currentFrame + 1), this);
    auto *scene = new QRadioButton(tr("Whole scene (1–%1)").arg(frameCount), this);
    auto *custom = new QRadioButton(tr("Frames"), this);
    m_range->addButton(current, CurrentFrame);
    m_range->addButton(scene, WholeScene);
    m_range->addButton(custom, CustomRange);
    m_from = new QSpinBox(this);
    m_to = new QSpinBox(this);
    for (QSpinBox *box : {m_from, m_to})
        box->setRange(1, kMaxFrame);
    m_from->setValue(1);
    m_to->setValue(frameCount);
    auto *customRow = new QHBoxLayout;
    customRow->addWidget(custom);
    customRow->addWidget(m_from);
    customRow->addWidget(new QLabel(tr("to"), this));
    customRow->addWidget(m_to);
    customRow->addStretch();
    auto *rangeBox = new QVBoxLayout;
    rangeBox->addWidget(current);
    rangeBox->addWidget(scene);
    rangeBox->addLayout(customRow);
    const int range = settings.value(kRangeKey, CurrentFrame).toInt();
    (m_range->button(range) ? m_range->button(range) : current)->setChecked(true);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setForegroundRole(QPalette::PlaceholderText);

    auto *form = new QFormLayout;
    form->addRow(tr("File:"), pathRow);
    form->addRow(tr("Format:"), m_format);
    form->addRow(QString(), m_transparent);
    form->addRow(QString(), m_compositing);
    form->addRow(tr("JPEG quality:"), m_quality);
    form->addRow(tr("Export:"), rangeBox);
    form->addRow(QString(), m_summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Export"));
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &ExportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(m_format, &QComboBox::currentIndexChanged, this, &ExportDialog::formatChanged);
    connect(m_path, &QLineEdit::textChanged, this, &ExportDialog::updateSummary);
    connect(m_range, &QButtonGroup::idToggled, this, &ExportDialog::updateSummary);
    connect(m_from, &QSpinBox::valueChanged, this, [this](int v) {
        m_to->setValue(std::max(m_to->value(), v));
        m_range->button(CustomRange)->setChecked(true);
        updateSummary();
    });
    connect(m_to, &QSpinBox::valueChanged, this, [this](int v) {
        m_from->setValue(std::min(m_from->value(), v));
        m_range->button(CustomRange)->setChecked(true);
        updateSummary();
    });
    formatChanged();
}

QString ExportDialog::path() const
{
    QString path = m_path->text().trimmed();
    const io::ImageFormat format = formatAt(m_format);
    if (!path.isEmpty() && io::formatForFile(path) != format)
        path += QLatin1Char('.') + io::suffixOf(format);
    return path;
}

io::ExportOptions ExportDialog::options() const
{
    io::ExportOptions o;
    o.format = formatAt(m_format);
    o.transparent = o.format == io::ImageFormat::Png && m_transparent->isChecked();
    o.quality = m_quality->value();
    o.compositing = m_compositing->isChecked();
    return o;
}

int ExportDialog::firstFrame() const
{
    switch (m_range->checkedId()) {
    case WholeScene: return 0;
    case CustomRange: return m_from->value() - 1;
    default: return m_currentFrame;
    }
}

int ExportDialog::lastFrame() const
{
    switch (m_range->checkedId()) {
    case WholeScene: return m_frameCount - 1;
    case CustomRange: return m_to->value() - 1;
    default: return m_currentFrame;
    }
}

void ExportDialog::browse()
{
    const QString png = tr("PNG image (*.png)"), jpeg = tr("JPEG image (*.jpg *.jpeg)");
    QString selected = formatAt(m_format) == io::ImageFormat::Jpeg ? jpeg : png;
    // The sequence's files are checked when exporting, not here.
    const QString file = QFileDialog::getSaveFileName(this, tr("Export frames"), path(), png + QStringLiteral(";;") + jpeg,
                                                      &selected, QFileDialog::DontConfirmOverwrite);
    if (file.isEmpty())
        return;
    const auto format = io::formatForFile(file).value_or(selected == jpeg ? io::ImageFormat::Jpeg : io::ImageFormat::Png);
    m_format->setCurrentIndex(m_format->findData(int(format)));
    m_path->setText(file);
}

void ExportDialog::formatChanged()
{
    const io::ImageFormat format = formatAt(m_format);
    m_transparent->setEnabled(format == io::ImageFormat::Png);
    m_quality->setEnabled(format == io::ImageFormat::Jpeg);
    // Keep the file name in step with the format.
    const QString text = m_path->text().trimmed();
    if (const auto current = io::formatForFile(text); current && *current != format) {
        const QFileInfo info(text);
        m_path->setText(info.dir().filePath(info.completeBaseName() + QLatin1Char('.') + io::suffixOf(format)));
    }
    updateSummary();
}

void ExportDialog::updateSummary()
{
    const QString file = path();
    if (file.isEmpty()) {
        m_summary->clear();
        return;
    }
    const int first = firstFrame(), last = lastFrame();
    if (first == last)
        m_summary->setText(tr("One image: %1").arg(QFileInfo(file).fileName()));
    else
        m_summary->setText(tr("%1 images: %2 … %3")
                               .arg(last - first + 1)
                               .arg(QFileInfo(io::sequenceFileName(file, first, last)).fileName(),
                                    QFileInfo(io::sequenceFileName(file, last, last)).fileName()));
}

void ExportDialog::accept()
{
    const QString file = path();
    if (file.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Choose a file name."));
        return;
    }
    if (!QFileInfo(file).dir().exists()) {
        QMessageBox::warning(this, windowTitle(), tr("The folder %1 does not exist.").arg(QFileInfo(file).path()));
        return;
    }
    int existing = 0;
    for (int f = firstFrame(); f <= lastFrame(); ++f)
        existing += QFileInfo::exists(isSequence() ? io::sequenceFileName(file, f, lastFrame()) : file) ? 1 : 0;
    if (existing > 0) {
        const QString question = isSequence() ? tr("%n file(s) of this sequence already exist. Replace them?", "", existing)
                                              : tr("%1 already exists. Replace it?").arg(QFileInfo(file).fileName());
        if (QMessageBox::question(this, windowTitle(), question) != QMessageBox::Yes)
            return;
    }

    QSettings settings;
    settings.setValue(kDirKey, QFileInfo(file).absolutePath());
    settings.setValue(kFormatKey, m_format->currentIndex());
    settings.setValue(kTransparentKey, m_transparent->isChecked());
    if (m_compositing->isEnabled())
        settings.setValue(kCompositingKey, m_compositing->isChecked());
    settings.setValue(kQualityKey, m_quality->value());
    settings.setValue(kRangeKey, m_range->checkedId());
    QDialog::accept();
}

} // namespace mothion::ui
