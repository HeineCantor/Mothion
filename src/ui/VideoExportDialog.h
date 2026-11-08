#pragma once

#include "io/VideoExport.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace mothion::ui {

// Asks where and how to export the animation as an mp4 video (through ffmpeg): the file, the frames (the whole
// scene or a range), the format (H.264 mp4, or VP9 webm and ProRes 4444 mov, which keep transparency, for animations
// without the paper), the quality, and whether the compositing network is applied. The choices are remembered.
class VideoExportDialog : public QDialog
{
    Q_OBJECT

public:
    // Frames are 0-based. `hasCompositing`: the project has a network to apply.
    VideoExportDialog(int frameCount, bool hasCompositing, const QString &projectName, QWidget *parent = nullptr);

    QString path() const;
    io::VideoOptions options() const;
    int firstFrame() const; // 0-based, inclusive
    int lastFrame() const;

    void accept() override;

private:
    void browse();
    void formatChanged(); // the suffix of the file, and what the format can do

    int m_frameCount;
    QLineEdit *m_path = nullptr;
    QRadioButton *m_whole = nullptr;
    QSpinBox *m_from = nullptr;
    QSpinBox *m_to = nullptr;
    QComboBox *m_format = nullptr;
    QComboBox *m_quality = nullptr;
    QCheckBox *m_withoutPaper = nullptr;
    QCheckBox *m_compositing = nullptr;
};

} // namespace mothion::ui
