#include "ui/LipSyncDialog.h"

#include "io/RhubarbLipSync.h"
#include "io/ZipaLipSync.h"

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
const QString kRecognizerKey = QStringLiteral("lipsync/recognizer");
const QString kMinFramesKey = QStringLiteral("lipsync/minFrames");

QString keyOf(io::LipSyncRecognizer r)
{
    switch (r) {
    case io::LipSyncRecognizer::Zipa: return QStringLiteral("zipa");
    case io::LipSyncRecognizer::RhubarbPhonetic: return QStringLiteral("rhubarb-phonetic");
    case io::LipSyncRecognizer::RhubarbEnglish: return QStringLiteral("rhubarb-english");
    }
    return {};
}
} // namespace

LipSyncDialog::LipSyncDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Lip sync from the audio"));
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("The project's audio is heard and the mouths of the current cel (a mouth chart) are put "
                                "on the frames where they are said. What is put can be changed by hand afterwards, and "
                                "the whole is one undo step."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    m_recognizer = new QComboBox(this);
    m_recognizer->setObjectName(QStringLiteral("lipSyncRecognizer"));
    m_recognizer->addItem(tr("ZIPA: any language, by its phones"), int(io::LipSyncRecognizer::Zipa));
    m_recognizer->addItem(tr("Rhubarb Lip Sync: any language, by its sounds"), int(io::LipSyncRecognizer::RhubarbPhonetic));
    m_recognizer->addItem(tr("Rhubarb Lip Sync: English, by its words"), int(io::LipSyncRecognizer::RhubarbEnglish));
    m_recognizer->setToolTip(tr("ZIPA hears the phones of any language, and is the most precise; Rhubarb Lip Sync is "
                                "the older way (by English words, it is precise in English only)"));
    form->addRow(tr("Heard by"), m_recognizer);
    m_dialog = new QPlainTextEdit(this);
    m_dialog->setObjectName(QStringLiteral("lipSyncDialogText"));
    m_dialog->setPlaceholderText(tr("What is said, if you know it (optional: Rhubarb gives better mouths with it)"));
    m_dialog->setFixedHeight(80);
    form->addRow(tr("Dialog"), m_dialog);
    m_minFrames = new QSpinBox(this);
    m_minFrames->setObjectName(QStringLiteral("lipSyncMinFrames"));
    m_minFrames->setRange(1, 6);
    m_minFrames->setSuffix(tr(" frame(s)"));
    m_minFrames->setToolTip(tr("A mouth that would last less is left out, the one before holds (2: on twos)"));
    form->addRow(tr("Each mouth at least"), m_minFrames);
    layout->addLayout(form);

    m_where = new QLabel(this);
    m_where->setObjectName(QStringLiteral("lipSyncWhere"));
    m_where->setWordWrap(true);
    m_where->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_where);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Lip sync"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);

    // The recognizer chosen last; at first ZIPA, or Rhubarb when this Mothion cannot run ZIPA.
    QSettings settings;
    const bool zipa = io::zipaBuiltIn() && !io::findZipaModel().isEmpty();
    const QString saved = settings.value(kRecognizerKey, keyOf(zipa ? io::LipSyncRecognizer::Zipa
                                                                    : io::LipSyncRecognizer::RhubarbPhonetic)).toString();
    for (int i = 0; i < m_recognizer->count(); ++i)
        if (keyOf(io::LipSyncRecognizer(m_recognizer->itemData(i).toInt())) == saved)
            m_recognizer->setCurrentIndex(i);
    m_minFrames->setValue(settings.value(kMinFramesKey, 1).toInt());
    connect(m_recognizer, &QComboBox::currentIndexChanged, this, &LipSyncDialog::recognizerChanged);
    recognizerChanged();
    resize(480, sizeHint().height());
}

io::LipSyncRecognizer LipSyncDialog::recognizer() const
{
    return io::LipSyncRecognizer(m_recognizer->currentData().toInt());
}

void LipSyncDialog::recognizerChanged()
{
    bool ready = false;
    if (recognizer() == io::LipSyncRecognizer::Zipa) {
        const QString model = io::findZipaModel();
        ready = io::zipaBuiltIn() && !model.isEmpty();
        m_where->setText(!io::zipaBuiltIn()
                             ? tr("This Mothion was built without ONNX Runtime, so ZIPA cannot run here: choose Rhubarb "
                                  "Lip Sync (or build with scripts/fetch-onnxruntime.sh run first).")
                         : model.isEmpty()
                             ? tr("The ZIPA model was not found. It comes with Mothion's releases; otherwise put "
                                  "model.int8.onnx and tokens.txt in a folder named \"zipa\" beside Mothion "
                                  "(scripts/fetch-zipa.sh), or set MOTHION_ZIPA to their folder.")
                             : tr("ZIPA (Zhu et al., CC BY 4.0): %1").arg(model));
    } else {
        const QString rhubarb = io::findRhubarb();
        ready = !rhubarb.isEmpty();
        m_where->setText(rhubarb.isEmpty()
                             ? tr("Rhubarb Lip Sync was not found. It comes with Mothion's releases; otherwise put it "
                                  "(with its \"res\" folder) in a folder named \"rhubarb\" beside Mothion, or set "
                                  "MOTHION_RHUBARB.")
                             : tr("Rhubarb Lip Sync: %1").arg(rhubarb));
    }
    m_dialog->setEnabled(recognizer() != io::LipSyncRecognizer::Zipa); // ZIPA hears the sounds alone
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ready);
}

io::LipSyncOptions LipSyncDialog::options() const
{
    io::LipSyncOptions o;
    o.recognizer = recognizer();
    if (o.recognizer != io::LipSyncRecognizer::Zipa)
        o.dialog = m_dialog->toPlainText();
    return o;
}

int LipSyncDialog::minFrames() const { return m_minFrames->value(); }

void LipSyncDialog::accept()
{
    QSettings settings;
    settings.setValue(kRecognizerKey, keyOf(recognizer()));
    settings.setValue(kMinFramesKey, m_minFrames->value());
    QDialog::accept();
}

} // namespace mothion::ui
