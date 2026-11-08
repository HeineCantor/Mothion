#include "ui/NewProjectDialog.h"

#include "tools/Preferences.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

constexpr auto kGroup = "newProject";

} // namespace

QList<NewProjectDialog::Preset> NewProjectDialog::presets()
{
    return {{tr("Full HD, 1920 × 1080"), 1920, 1080}, {tr("HD, 1280 × 720"), 1280, 720},
            {tr("4K UHD, 3840 × 2160"), 3840, 2160},  {tr("2K DCI, 2048 × 1080"), 2048, 1080},
            {tr("Vertical, 1080 × 1920"), 1080, 1920}, {tr("Square, 1080 × 1080"), 1080, 1080},
            {tr("Custom"), 0, 0}};
}

NewProjectDialog::NewProjectDialog(QWidget *parent)
    : QDialog(parent)
    , m_preset(new QComboBox(this))
    , m_width(new QSpinBox(this))
    , m_height(new QSpinBox(this))
    , m_fps(new QSpinBox(this))
    , m_frames(new QSpinBox(this))
    , m_paperButton(new QPushButton(this))
    , m_transparent(new QCheckBox(tr("Transparent"), this))
    , m_summary(new QLabel(this))
{
    setObjectName(QStringLiteral("NewProjectDialog"));
    setWindowTitle(tr("New project"));
    const tools::Preferences &prefs = tools::Preferences::instance();
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));

    m_preset->setObjectName(QStringLiteral("preset"));
    for (const Preset &p : presets())
        m_preset->addItem(p.name);
    m_width->setObjectName(QStringLiteral("width"));
    m_height->setObjectName(QStringLiteral("height"));
    for (QSpinBox *box : {m_width, m_height}) {
        box->setRange(16, 16000);
        box->setSuffix(QStringLiteral(" px"));
        box->setAccelerated(true);
    }
    m_width->setValue(settings.value(QStringLiteral("width"), prefs.newWidth()).toInt());
    m_height->setValue(settings.value(QStringLiteral("height"), prefs.newHeight()).toInt());
    m_fps->setObjectName(QStringLiteral("fps"));
    m_fps->setRange(1, 120);
    m_fps->setSuffix(tr(" fps"));
    m_fps->setValue(settings.value(QStringLiteral("fps"), prefs.newFrameRate()).toInt());
    m_frames->setObjectName(QStringLiteral("frames"));
    m_frames->setRange(1, 5000);
    m_frames->setSuffix(tr(" frames"));
    m_frames->setValue(settings.value(QStringLiteral("frames"), prefs.newFrames()).toInt());
    m_paper = QColor(settings.value(QStringLiteral("paper"), prefs.paperColor().name()).toString());
    if (!m_paper.isValid())
        m_paper = prefs.paperColor();
    m_paperButton->setObjectName(QStringLiteral("paper"));
    m_transparent->setObjectName(QStringLiteral("transparent"));
    m_transparent->setToolTip(tr("No paper: the page is see-through (shown on squares), and PNG frames keep it. JPEG "
                                 "and video have no alpha: they get white there."));
    m_transparent->setChecked(settings.value(QStringLiteral("paperTransparent"), prefs.paperColor().alpha() < 255).toBool());
    m_summary->setObjectName(QStringLiteral("summary"));
    m_summary->setEnabled(false); // a note, in the dimmer color
    m_summary->setWordWrap(true);

    // The size: width and height side by side, with a button to turn the page the other way.
    auto *swap = new QToolButton(this);
    swap->setObjectName(QStringLiteral("swap"));
    swap->setText(QStringLiteral("⇄"));
    swap->setToolTip(tr("Swap the width and the height"));
    auto *size = new QHBoxLayout;
    size->addWidget(m_width, 1);
    size->addWidget(swap);
    size->addWidget(m_height, 1);

    auto *form = new QFormLayout;
    form->addRow(tr("Page"), m_preset);
    form->addRow(tr("Size"), size);
    form->addRow(tr("Frame rate"), m_fps);
    form->addRow(tr("Length"), m_frames);
    auto *paperRow = new QHBoxLayout;
    paperRow->addWidget(m_paperButton, 1);
    paperRow->addWidget(m_transparent);
    form->addRow(tr("Paper"), paperRow);
    form->addRow(m_summary);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(m_preset, &QComboBox::activated, this, &NewProjectDialog::presetChosen);
    connect(m_width, &QSpinBox::valueChanged, this, &NewProjectDialog::sizeEdited);
    connect(m_height, &QSpinBox::valueChanged, this, &NewProjectDialog::sizeEdited);
    connect(swap, &QToolButton::clicked, this, [this] {
        const int w = m_width->value();
        m_width->setValue(m_height->value()); // each of these shows the summary: the last one is right
        m_height->setValue(w);
    });
    connect(m_fps, &QSpinBox::valueChanged, this, &NewProjectDialog::showSummary);
    connect(m_frames, &QSpinBox::valueChanged, this, &NewProjectDialog::showSummary);
    connect(m_paperButton, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_paper, this, tr("Paper color"));
        if (c.isValid()) {
            m_paper = c;
            showPaper();
        }
    });
    connect(m_transparent, &QCheckBox::toggled, this, &NewProjectDialog::showPaper);
    sizeEdited(); // the preset that matches the size, or Custom
    showPaper();
}

int NewProjectDialog::pageWidth() const { return m_width->value(); }
int NewProjectDialog::pageHeight() const { return m_height->value(); }
int NewProjectDialog::frameRate() const { return m_fps->value(); }
int NewProjectDialog::frames() const { return m_frames->value(); }

void NewProjectDialog::presetChosen()
{
    const Preset preset = presets().value(m_preset->currentIndex());
    if (preset.width <= 0) // Custom: the size stays as it is, to be edited
        return;
    const QSignalBlocker blockWidth(m_width), blockHeight(m_height);
    m_width->setValue(preset.width);
    m_height->setValue(preset.height);
    showSummary();
}

void NewProjectDialog::sizeEdited()
{
    // The preset that has this size, or Custom.
    const QList<Preset> all = presets();
    int index = int(all.size()) - 1;
    for (int i = 0; i < all.size(); ++i)
        if (all[i].width == m_width->value() && all[i].height == m_height->value())
            index = i;
    const QSignalBlocker block(m_preset);
    m_preset->setCurrentIndex(index);
    showSummary();
}

void NewProjectDialog::showSummary()
{
    const double megapixels = double(m_width->value()) * m_height->value() / 1e6;
    const double seconds = double(m_frames->value()) / m_fps->value();
    m_summary->setText(tr("%1 megapixels, %2 s at %3 fps")
                           .arg(megapixels, 0, 'f', 1)
                           .arg(seconds, 0, 'g', 4)
                           .arg(m_fps->value()));
}

QColor NewProjectDialog::paper() const
{
    QColor color = m_paper;
    color.setAlpha(m_transparent->isChecked() ? 0 : 255);
    return color;
}

void NewProjectDialog::showPaper()
{
    QPixmap swatch(40, 16);
    if (m_transparent->isChecked()) { // squares, as see-through is shown everywhere
        swatch.fill(Qt::white);
        QPainter p(&swatch);
        for (int y = 0; y < 16; y += 4)
            for (int x = ((y / 4) % 2) * 4; x < 40; x += 8)
                p.fillRect(x, y, 4, 4, QColor(0xcc, 0xcc, 0xcc));
    } else {
        swatch.fill(m_paper);
    }
    m_paperButton->setIcon(QIcon(swatch));
    m_paperButton->setText(m_transparent->isChecked() ? tr("No paper") : m_paper.name());
    m_paperButton->setEnabled(!m_transparent->isChecked());
}

void NewProjectDialog::accept()
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QStringLiteral("width"), m_width->value());
    settings.setValue(QStringLiteral("height"), m_height->value());
    settings.setValue(QStringLiteral("fps"), m_fps->value());
    settings.setValue(QStringLiteral("frames"), m_frames->value());
    settings.setValue(QStringLiteral("paper"), m_paper.name());
    settings.setValue(QStringLiteral("paperTransparent"), m_transparent->isChecked());
    QDialog::accept();
}

} // namespace mothion::ui
