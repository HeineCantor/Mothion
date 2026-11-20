#pragma once

#include "io/LipSyncJob.h"

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QPlainTextEdit;
class QSpinBox;

namespace mothion::ui {

// Lip sync from the audio track: who hears it (ZIPA, any language, the default; or Rhubarb Lip Sync, by the sounds or
// by English words), what is said (optional, for Rhubarb), and how many frames each mouth holds at least (2: on twos).
// The recognizer and the frames are remembered. Says where the recognizer is, or that it is missing, and cannot be run
// without it.
class LipSyncDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LipSyncDialog(QWidget *parent = nullptr);

    io::LipSyncOptions options() const; // the extended shapes left as they are: the caller knows the mouths
    int minFrames() const;

    void accept() override;

private:
    io::LipSyncRecognizer recognizer() const;
    void recognizerChanged();

    QComboBox *m_recognizer = nullptr;
    QPlainTextEdit *m_dialog = nullptr;
    QSpinBox *m_minFrames = nullptr;
    QLabel *m_where = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace mothion::ui
