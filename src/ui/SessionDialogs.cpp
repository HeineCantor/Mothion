#include "ui/SessionDialogs.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSpinBox>
#include <QSysInfo>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

constexpr auto kGroup = "collaboration";

QSpinBox *portBox(QWidget *parent, int value)
{
    auto *box = new QSpinBox(parent);
    box->setRange(1024, 65535);
    box->setValue(value);
    return box;
}

} // namespace

QString defaultPersonName()
{
    for (const char *variable : {"USER", "USERNAME"}) {
        const QString user = qEnvironmentVariable(variable);
        if (!user.isEmpty())
            return user;
    }
    return QSysInfo::machineHostName();
}

QString makeInvitationCode()
{
    static const QString letters = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZ23456789"); // no 0/O, 1/I
    QString code;
    for (int i = 0; i < 6; ++i)
        code += letters.at(int(QRandomGenerator::system()->bounded(letters.size())));
    return code;
}

QStringList localAddresses()
{
    QStringList addresses;
    for (const QHostAddress &address : QNetworkInterface::allAddresses())
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            addresses << address.toString();
    return addresses;
}

// --- HostSessionDialog -------------------------------------------------------------------

HostSessionDialog::HostSessionDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("HostSessionDialog"));
    setWindowTitle(tr("Host a session"));
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    m_name = new QLineEdit(settings.value(QStringLiteral("name"), defaultPersonName()).toString(), this);
    m_name->setObjectName(QStringLiteral("name"));
    m_port = portBox(this, settings.value(QStringLiteral("hostPort"), kDefaultSessionPort).toInt());
    m_port->setObjectName(QStringLiteral("port"));
    m_code = new QLineEdit(makeInvitationCode(), this); // a new one each time: a code is for one session
    m_code->setObjectName(QStringLiteral("code"));
    auto *another = new QPushButton(tr("New code"), this);
    connect(another, &QPushButton::clicked, this, [this] { m_code->setText(makeInvitationCode()); });
    auto *codeRow = new QHBoxLayout;
    codeRow->addWidget(m_code, 1);
    codeRow->addWidget(another);

    const QStringList addresses = localAddresses();
    auto *where = new QLabel(addresses.isEmpty() ? tr("No network address was found: the others cannot reach you.")
                                                 : tr("The others join at: %1").arg(addresses.join(QStringLiteral(", "))),
                             this);
    where->setObjectName(QStringLiteral("addresses"));
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *note = new QLabel(tr("The project in this window is what is shared. Over the Internet, the port has to be "
                               "open on your router, or both of you on the same VPN."),
                            this);
    note->setWordWrap(true);
    note->setEnabled(false);

    auto *form = new QFormLayout;
    form->addRow(tr("Your name"), m_name);
    form->addRow(tr("Port"), m_port);
    form->addRow(tr("Invitation code"), codeRow);
    form->addRow(where);
    form->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Start"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString HostSessionDialog::name() const { return m_name->text().trimmed(); }
quint16 HostSessionDialog::port() const { return quint16(m_port->value()); }
QString HostSessionDialog::code() const { return m_code->text().trimmed(); }

void HostSessionDialog::accept()
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QStringLiteral("name"), name());
    settings.setValue(QStringLiteral("hostPort"), m_port->value());
    QDialog::accept();
}

// --- JoinSessionDialog ----------------------------------------------------------------------

JoinSessionDialog::JoinSessionDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("JoinSessionDialog"));
    setWindowTitle(tr("Join a session"));
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    m_name = new QLineEdit(settings.value(QStringLiteral("name"), defaultPersonName()).toString(), this);
    m_name->setObjectName(QStringLiteral("name"));
    m_host = new QLineEdit(settings.value(QStringLiteral("joinHost")).toString(), this);
    m_host->setObjectName(QStringLiteral("host"));
    m_host->setPlaceholderText(QStringLiteral("192.168.1.5"));
    m_port = portBox(this, settings.value(QStringLiteral("joinPort"), kDefaultSessionPort).toInt());
    m_port->setObjectName(QStringLiteral("port"));
    m_code = new QLineEdit(this);
    m_code->setObjectName(QStringLiteral("code"));
    auto *note = new QLabel(tr("Joining replaces the project in this window with the host's: it is asked first if it "
                               "has unsaved changes."),
                            this);
    note->setWordWrap(true);
    note->setEnabled(false);

    auto *form = new QFormLayout;
    form->addRow(tr("Your name"), m_name);
    form->addRow(tr("Host address"), m_host);
    form->addRow(tr("Port"), m_port);
    form->addRow(tr("Invitation code"), m_code);
    form->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Join"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString JoinSessionDialog::name() const { return m_name->text().trimmed(); }

QString JoinSessionDialog::host() const
{
    QString text = m_host->text().trimmed();
    const int colon = text.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && text.count(QLatin1Char(':')) == 1) // "address:port"
        text.truncate(colon);
    return text;
}

quint16 JoinSessionDialog::port() const
{
    const QString text = m_host->text().trimmed();
    const int colon = text.lastIndexOf(QLatin1Char(':'));
    if (colon > 0 && text.count(QLatin1Char(':')) == 1) {
        bool ok = false;
        const int typed = text.mid(colon + 1).toInt(&ok);
        if (ok && typed > 0 && typed < 65536)
            return quint16(typed);
    }
    return quint16(m_port->value());
}

QString JoinSessionDialog::code() const { return m_code->text().trimmed(); }

void JoinSessionDialog::accept()
{
    if (host().isEmpty()) {
        m_host->setFocus();
        return;
    }
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    settings.setValue(QStringLiteral("name"), name());
    settings.setValue(QStringLiteral("joinHost"), host());
    settings.setValue(QStringLiteral("joinPort"), int(port()));
    QDialog::accept();
}

} // namespace mothion::ui
