#include "ui/BlenderSessionDialog.h"

#include "io/BlenderAddOn.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mothion::ui {

BlenderSessionDialog::BlenderSessionDialog(io::BlenderLinkServer *server, int currentFrame, QWidget *parent)
    : QDialog(parent)
    , m_server(server)
    , m_list(new QListWidget(this))
    , m_empty(new QLabel(this))
    , m_start(new QSpinBox(this))
{
    setObjectName(QStringLiteral("BlenderSessionDialog"));
    setWindowTitle(tr("Import from Blender"));
    m_list->setObjectName(QStringLiteral("sessions"));
    m_start->setObjectName(QStringLiteral("start"));
    m_start->setRange(1, 99999);
    m_start->setValue(currentFrame + 1);
    m_empty->setObjectName(QStringLiteral("empty"));
    m_empty->setWordWrap(true);
    m_empty->setText(tr("No Blender is connected. Start Blender with the Mothion Link add-on on (install it with the button "
                        "below, once), and open the scene: it appears here."));

    auto *install = new QPushButton(tr("Install the add-on in Blender…"), this);
    install->setObjectName(QStringLiteral("install"));
    install->setFocusPolicy(Qt::NoFocus);
    connect(install, &QPushButton::clicked, this, &BlenderSessionDialog::installAddOn);

    auto *form = new QFormLayout;
    form->addRow(tr("Start at frame"), m_start);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("The scene is brought in as a new cel, with a frame for each frame of the scene, as the 3D "
                                    "viewport of Blender shows it through the active camera. The cel follows the scene as you "
                                    "change it."),
                                 this));
    layout->addWidget(m_list);
    layout->addWidget(m_empty);
    layout->addLayout(form);
    layout->addWidget(install, 0, Qt::AlignLeft);
    layout->addWidget(buttons);
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { m_ok->setEnabled(sessionId() != 0); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    for (auto signal : {&io::BlenderLinkServer::sessionConnected, &io::BlenderLinkServer::sessionUpdated,
                        &io::BlenderLinkServer::sessionClosed})
        connect(m_server, signal, this, &BlenderSessionDialog::fill);
    fill();
}

void BlenderSessionDialog::fill()
{
    const int chosen = sessionId();
    m_list->clear();
    for (const io::BlenderSession &s : m_server->sessions()) {
        const QString file = s.file.isEmpty() ? tr("(not saved yet)") : QFileInfo(s.file).fileName();
        auto *item = new QListWidgetItem(tr("%1 — scene “%2” — %n frame(s), camera “%3”", nullptr, s.frameCount())
                                             .arg(file, s.scene, s.camera.isEmpty() ? tr("none") : s.camera),
                                         m_list);
        item->setData(Qt::UserRole, s.id);
        item->setToolTip(s.file);
        if (!s.viewport || s.camera.isEmpty() || s.frameCount() <= 0)
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled); // nothing to bring in from it
        if (s.id == chosen)
            m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem())
        for (int i = 0; i < m_list->count(); ++i)
            if (m_list->item(i)->flags() & Qt::ItemIsEnabled) {
                m_list->setCurrentRow(i);
                break;
            }
    m_empty->setVisible(m_list->count() == 0);
    m_list->setVisible(m_list->count() > 0);
    m_ok->setEnabled(sessionId() != 0);
}

int BlenderSessionDialog::sessionId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item && (item->flags() & Qt::ItemIsEnabled) ? item->data(Qt::UserRole).toInt() : 0;
}

int BlenderSessionDialog::startFrame() const
{
    return m_start->value() - 1;
}

void BlenderSessionDialog::installAddOn()
{
    QSettings settings;
    QString blender = io::findBlender(settings.value(QStringLiteral("blender/path")).toString());
    if (blender.isEmpty()) {
        QMessageBox::information(this, tr("Install the add-on"),
                                 tr("Blender was not found. Choose its program (blender, or blender.exe)."));
        blender = QFileDialog::getOpenFileName(this, tr("Where is Blender?"));
        if (blender.isEmpty() || io::findBlender(blender).isEmpty())
            return;
        settings.setValue(QStringLiteral("blender/path"), blender);
    }
    QString message;
    const bool ok = io::installBlenderAddOn(blender, &message);
    if (ok)
        QMessageBox::information(this, tr("Install the add-on"), message);
    else
        QMessageBox::warning(this, tr("Install the add-on"), message);
}

} // namespace mothion::ui
