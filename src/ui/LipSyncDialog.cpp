#include "ui/LipSyncDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {
const QString kEnglishKey = QStringLiteral("lipsync/english");
const QString kMinFramesKey = QStringLiteral("lipsync/minFrames");
} // namespace

LipSyncDialog::LipSyncDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Lip sync from the audio"));
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Rhubarb Lip Sync hears the project's audio and puts the mouths of the current cel (a "
                                "mouth chart) on the frames where they are said. What it puts can be changed by hand "
                                "afterwards, and the whole is one undo step."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    m_dialog = new QPlainTextEdit(this);
    m_dialog->setObjectName(QStringLiteral("lipSyncDialogText"));
    m_dialog->setPlaceholderText(tr("What is said, if you know it (optional: it gives better mouths)"));
    m_dialog->setFixedHeight(80);
    form->addRow(tr("Dialog"), m_dialog);
    m_language = new QComboBox(this);
    m_language->setObjectName(QStringLiteral("lipSyncLanguage"));
    m_language->addItem(tr("Any language (by its sounds)"));
    m_language->addItem(tr("English (by its words: more precise)"));
    form->addRow(tr("Language"), m_language);
    m_minFrames = new QSpinBox(this);
    m_minFrames->setObjectName(QStringLiteral("lipSyncMinFrames"));
    m_minFrames->setRange(1, 6);
    m_minFrames->setSuffix(tr(" frame(s)"));
    m_minFrames->setToolTip(tr("A mouth that would last less is left out, the one before holds (2: on twos)"));
    form->addRow(tr("Each mouth at least"), m_minFrames);
    layout->addLayout(form);

    QSettings settings;
    m_language->setCurrentIndex(settings.value(kEnglishKey, false).toBool() ? 1 : 0);
    m_minFrames->setValue(settings.value(kMinFramesKey, 1).toInt());

    const QString rhubarb = io::findRhubarb();
    auto *where = new QLabel(this);
    where->setWordWrap(true);
    where->setForegroundRole(QPalette::PlaceholderText);
    where->setText(rhubarb.isEmpty()
                       ? tr("Rhubarb Lip Sync was not found. It comes with Mothion's releases; otherwise put it (with "
                            "its \"res\" folder) in a folder named \"rhubarb\" beside Mothion, or set MOTHION_RHUBARB.")
                       : tr("Rhubarb Lip Sync: %1").arg(rhubarb));
    layout->addWidget(where);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Lip sync"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!rhubarb.isEmpty());
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(460, sizeHint().height());
}

io::LipSyncOptions LipSyncDialog::options() const
{
    io::LipSyncOptions o;
    o.dialog = m_dialog->toPlainText();
    o.english = m_language->currentIndex() == 1;
    return o;
}

int LipSyncDialog::minFrames() const { return m_minFrames->value(); }

void LipSyncDialog::accept()
{
    QSettings settings;
    settings.setValue(kEnglishKey, m_language->currentIndex() == 1);
    settings.setValue(kMinFramesKey, m_minFrames->value());
    QDialog::accept();
}

} // namespace mothion::ui
