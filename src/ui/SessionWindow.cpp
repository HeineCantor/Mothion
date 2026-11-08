#include "ui/SessionWindow.h"

#include "sync/SyncClient.h"
#include "sync/SyncHost.h"
#include "ui/SessionDialogs.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace mothion::ui {

SessionWindow::SessionWindow(sync::SyncHost *host, sync::SyncClient *client, QWidget *parent)
    : QDialog(parent)
    , m_host(host)
    , m_client(client)
    , m_status(new QLabel(this))
    , m_invitation(new QLabel(this))
    , m_copy(new QPushButton(tr("Copy the invitation"), this))
    , m_people(new QListWidget(this))
    , m_end(new QPushButton(this))
{
    setObjectName(QStringLiteral("SessionWindow"));
    setWindowTitle(tr("Shared session"));
    setModal(false);
    m_status->setObjectName(QStringLiteral("status"));
    m_status->setWordWrap(true);
    m_invitation->setObjectName(QStringLiteral("invitation"));
    m_invitation->setWordWrap(true);
    m_invitation->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_copy->setObjectName(QStringLiteral("copy"));
    m_people->setObjectName(QStringLiteral("people"));
    m_people->setSelectionMode(QAbstractItemView::NoSelection);
    m_end->setObjectName(QStringLiteral("end"));

    connect(m_copy, &QPushButton::clicked, this, [this] {
        QStringList addresses = localAddresses();
        if (addresses.isEmpty())
            addresses << QStringLiteral("localhost");
        QGuiApplication::clipboard()->setText(
            tr("Mothion session at %1:%2, code %3").arg(addresses.first()).arg(m_port).arg(m_code));
    });
    connect(m_end, &QPushButton::clicked, this, &SessionWindow::endRequested);
    for (const QMetaObject::Connection &c : {
             connect(m_host, &sync::SyncHost::participantsChanged, this, &SessionWindow::refresh),
             connect(m_host, &sync::SyncHost::locksChanged, this, &SessionWindow::refresh),
             connect(m_client, &sync::SyncClient::participantsChanged, this, &SessionWindow::refresh),
             connect(m_client, &sync::SyncClient::joined, this, &SessionWindow::refresh)})
        Q_UNUSED(c)

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_copy);
    buttons->addStretch(1);
    buttons->addWidget(m_end);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_invitation);
    layout->addWidget(new QLabel(tr("People:"), this));
    layout->addWidget(m_people, 1);
    layout->addLayout(buttons);
    resize(360, 300);
    refresh();
}

void SessionWindow::setInvitation(const QString &code, quint16 port)
{
    m_code = code;
    m_port = port;
    refresh();
}

void SessionWindow::refresh()
{
    const bool hosting = m_host->isSharing();
    const bool guest = m_client->isJoined();
    QStringList names = hosting ? m_host->participantNames() : guest ? m_client->participantNames() : QStringList();
    m_people->clear();
    for (const QString &name : names)
        m_people->addItem(name);
    if (hosting) {
        const QStringList addresses = localAddresses();
        m_status->setText(tr("You are hosting this project. %n person(s) here.", nullptr, int(names.size())));
        m_invitation->setText(tr("Address: %1, port %2\nInvitation code: %3")
                                  .arg(addresses.isEmpty() ? tr("(none found)") : addresses.join(QStringLiteral(", ")))
                                  .arg(m_port)
                                  .arg(m_code));
        m_end->setText(tr("Stop sharing"));
    } else if (guest) {
        m_status->setText(tr("You joined the session of %1. %n person(s) here.", nullptr, int(names.size()))
                              .arg(m_client->hostName()));
        m_invitation->clear();
        m_end->setText(tr("Leave the session"));
    } else {
        m_status->setText(tr("No session."));
        m_invitation->clear();
        m_end->setText(tr("Close"));
    }
    m_invitation->setVisible(hosting);
    m_copy->setVisible(hosting);
    m_end->setEnabled(hosting || guest);
}

} // namespace mothion::ui
