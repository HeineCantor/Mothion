#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>

namespace mothion::ai {

// The ComfyUI server Mothion starts and stops for the person: ComfyUI is run from the folder the install script made
// (its own Python and main.py: Settings > AI says where), listening on the address of the settings, and stopped with
// Mothion (a server started by hand elsewhere is left alone, and shown as running).
// What it knows comes from asking the server: Stopped (no answer), Starting (started here, not answering yet: it
// takes a while to load), Running (answers).
class ServerProcess : public QObject
{
    Q_OBJECT

public:
    enum class State { Stopped, Starting, Running };
    explicit ServerProcess(QObject *parent = nullptr);
    ~ServerProcess() override;

    State state() const { return m_state; }
    bool startedHere() const { return m_process.state() != QProcess::NotRunning; }
    // Why it stopped by itself or could not start (the end of what ComfyUI said); empty if nothing is wrong.
    QString problem() const { return m_problem; }
    // Whether Start can work: ComfyUI is installed in the folder, and the address is this computer's.
    bool canStart(QString *why = nullptr) const;

    // While somebody looks (the window is open) the state is checked every moment, also with nothing starting.
    void setWatching(bool watching);

public slots:
    void start();
    void stop();
    void check(); // asks the server now

signals:
    void changed();

private:
    void setState(State state);
    void onAnswer(bool answered);
    QString program() const;
    QString script() const;

    QNetworkAccessManager m_network;
    QProcess m_process;
    QTimer m_poll;
    QByteArray m_output; // the end of what ComfyUI printed
    QString m_problem;
    State m_state = State::Stopped;
    bool m_asking = false;
    bool m_watching = false;
    bool m_stopping = false;
};

} // namespace mothion::ai
