#pragma once

#include "ai/Reference.h"

#include <QDialog>
#include <QTimer>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

namespace mothion::ai {
class ComfyClient;
class ServerProcess;
}
namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// Edit > Refine with AI: the picture that will go to the workflow (the layers chosen in the Layers panel, and the
// area under the selection, as they are now: it follows them while the window is open), the prompt and the negative
// to add to the standing ones of Settings > AI, and Generate. While ComfyUI works a bar shows; the result becomes a
// layer on top of the frame the picture was taken from.
class RefineDialog : public QDialog
{
    Q_OBJECT

public:
    // `server` starts and stops ComfyUI from the window; it lives on (with the main window) when this one is closed.
    RefineDialog(document::DocumentController *controller, ai::ServerProcess *server, QWidget *parent = nullptr);

    ai::ComfyClient *client() const { return m_client; }
    const ai::Reference &reference() const { return m_reference; }
    bool generating() const { return m_generating; }

public slots:
    void refreshReference(); // now (otherwise a moment after the last change)

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void generate();
    void stop();
    void finish(const QImage &result);
    void failWith(const QString &message);
    void setBusy(bool busy);
    void showReference();
    void showServer();

    document::DocumentController *m_controller;
    ai::ComfyClient *m_client;
    ai::ServerProcess *m_server;
    QLabel *m_serverState;
    QPushButton *m_start;
    QPushButton *m_stopServer;
    QLabel *m_preview;
    QLabel *m_info;
    QPlainTextEdit *m_prompt;
    QPlainTextEdit *m_negative;
    QLabel *m_status;
    QProgressBar *m_bar;
    QPushButton *m_generate;
    QPushButton *m_stop;
    QTimer m_later;
    ai::Reference m_reference; // what the picture is now
    ai::Reference m_job;       // what was sent (the result goes with it)
    bool m_generating = false;
};

} // namespace mothion::ui
