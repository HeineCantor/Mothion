#pragma once

#include "io/RhubarbLipSync.h"

#include <QDialog>

class QComboBox;
class QPlainTextEdit;
class QSpinBox;

namespace mothion::ui {

// Lip sync from the audio track (Rhubarb Lip Sync): what is said (optional, it helps), its language (English words,
// or any language by its sounds), and how many frames each mouth holds at least (2: on twos). The language and the
// frames are remembered. Says so, and cannot be run, when Rhubarb is not found.
class LipSyncDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LipSyncDialog(QWidget *parent = nullptr);

    io::LipSyncOptions options() const; // the extended shapes left as they are: the caller knows the mouths
    int minFrames() const;

    void accept() override;

private:
    QPlainTextEdit *m_dialog = nullptr;
    QComboBox *m_language = nullptr;
    QSpinBox *m_minFrames = nullptr;
};

} // namespace mothion::ui
