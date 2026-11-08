#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QListWidget;
class QPushButton;

namespace mothion::sync {
class SyncClient;
class SyncHost;
} // namespace mothion::sync

namespace mothion::ui {

// The window of a shared session, which stays open beside the work: who is in it, and, for the host, how the others
// join (address, port and code, to copy), with the button that ends it (stops sharing, or leaves).
class SessionWindow : public QDialog
{
    Q_OBJECT

public:
    SessionWindow(sync::SyncHost *host, sync::SyncClient *client, QWidget *parent = nullptr);

    // What to tell guests of a session being hosted (the code is the host's choice).
    void setInvitation(const QString &code, quint16 port);

public slots:
    void refresh();

signals:
    void endRequested();

private:
    sync::SyncHost *m_host;
    sync::SyncClient *m_client;
    QString m_code;
    quint16 m_port = 0;
    QLabel *m_status;
    QLabel *m_invitation;
    QPushButton *m_copy;
    QListWidget *m_people;
    QPushButton *m_end;
};

} // namespace mothion::ui
