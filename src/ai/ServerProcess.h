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
// takes a while to load), Running (answers). Installing is the install script running (see install()).
//
// Not installed (or installed by an older script: see kInstallStamp) is not a dead end: start() installs ComfyUI with
// the models and the workflows first (the scripts are in the program: scripts/comfyui), then starts it.
class ServerProcess : public QObject
{
    Q_OBJECT

public:
    enum class State { Stopped, Starting, Running, Installing };
    // What the install script writes in <folder>/.mothion-install when it is done. Raised when the script gains
    // something (a new custom node, a new model): an older install then asks to be completed.
    static constexpr int kInstallStamp = 1;
    explicit ServerProcess(QObject *parent = nullptr);
    ~ServerProcess() override;

    State state() const { return m_state; }
    bool startedHere() const { return m_process.state() != QProcess::NotRunning; }
    // Why it stopped by itself or could not start (the end of what ComfyUI said); empty if nothing is wrong.
    QString problem() const { return m_problem; }
    // Whether Start can work: the address is this computer's (and there is a way to install, if it is not installed).
    bool canStart(QString *why = nullptr) const;
    // ComfyUI is in the folder, complete (the install script finished, in its current version).
    bool installed() const;
    // What the installer is doing now (its last step), and how far a download is (0-100; -1: not known).
    QString installStatus() const { return m_installStatus; }
    int installPercent() const { return m_installPercent; }
    // Writes the scripts that are in the program (install.sh, install.ps1 and the workflows) into `folder`, as they
    // are in scripts/comfyui. False if one cannot be written.
    static bool extractScripts(const QString &folder);
    // For tests: another program to run as the install script.
    void setInstallCommand(const QString &program, const QStringList &arguments);

    // While somebody looks (the window is open) the state is checked every moment, also with nothing starting.
    void setWatching(bool watching);

public slots:
    // Starts the server; if it is not installed, installs it first (and starts it when that is done).
    void start();
    // Installs ComfyUI in the folder (the scripts, with the models); starts it afterwards.
    void install();
    void stop();
    void check(); // asks the server now

signals:
    void changed();

private:
    void setState(State state);
    void onAnswer(bool answered);
    void installFinished(int code, QProcess::ExitStatus status);
    void readInstaller();
    void launch();
    void killInstaller();
    QString program() const;
    QString script() const;

    QNetworkAccessManager m_network;
    QProcess m_process;
    QProcess m_installer;
    QString m_scriptsDir;
    QString m_installProgram;
    QStringList m_installArguments;
    QString m_installStatus;
    QByteArray m_installOutput;
    int m_installPercent = -1;
    bool m_installCancelled = false;
    QTimer m_poll;
    QByteArray m_output; // the end of what ComfyUI printed
    QString m_problem;
    State m_state = State::Stopped;
    bool m_asking = false;
    bool m_watching = false;
    bool m_stopping = false;
};

} // namespace mothion::ai
