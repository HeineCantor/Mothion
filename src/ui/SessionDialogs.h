#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QSpinBox;

namespace mothion::ui {

// The names and codes a shared session is made of, with what the last time used remembered.
constexpr int kDefaultSessionPort = 4848;

// A name for the person, to start from: the user of the system.
QString defaultPersonName();
// A short code to invite with (letters and digits that are not mistaken for one another).
QString makeInvitationCode();
// The addresses the others can try to reach this computer on, in the local network (no loopback).
QStringList localAddresses();

// What the host chooses before opening a session: its name, the port the others connect to, and the invitation
// code they must give. The project in the window is what is shared.
class HostSessionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HostSessionDialog(QWidget *parent = nullptr);

    QString name() const;
    quint16 port() const;
    QString code() const;

    void accept() override; // remembers the choices

private:
    QLineEdit *m_name;
    QSpinBox *m_port;
    QLineEdit *m_code;
};

// What a guest gives to join: where the host is (an address, and the port), the invitation code, and its own name.
// The address may be typed with the port ("192.168.1.5:4848").
class JoinSessionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit JoinSessionDialog(QWidget *parent = nullptr);

    QString name() const;
    QString host() const; // without the port
    quint16 port() const;
    QString code() const;

    void accept() override;

private:
    QLineEdit *m_name;
    QLineEdit *m_host;
    QSpinBox *m_port;
    QLineEdit *m_code;
};

} // namespace mothion::ui
