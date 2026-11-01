#pragma once

#include "io/FrameExport.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace mothion::ui {

// Asks where and how to export frames as images: the file, PNG or JPEG
// (transparent background for PNG, quality for JPEG) and which frames:
// the current one, the whole scene or a range. Several frames make a
// numbered sequence (io::sequenceFileName). The choices are remembered.
class ExportDialog : public QDialog
{
    Q_OBJECT

public:
    // Frames are 0-based; `projectName` names the files the first time.
    ExportDialog(int currentFrame, int frameCount, const QString &projectName, QWidget *parent = nullptr);

    QString path() const;
    io::ExportOptions options() const;
    int firstFrame() const; // 0-based, inclusive
    int lastFrame() const;
    bool isSequence() const { return lastFrame() > firstFrame(); }

    void accept() override;

private:
    void browse();
    void formatChanged();
    void updateSummary();

    int m_currentFrame;
    int m_frameCount;
    QLineEdit *m_path = nullptr;
    QComboBox *m_format = nullptr;
    QCheckBox *m_transparent = nullptr;
    QSpinBox *m_quality = nullptr;
    QButtonGroup *m_range = nullptr;
    QSpinBox *m_from = nullptr;
    QSpinBox *m_to = nullptr;
    QLabel *m_summary = nullptr;
};

} // namespace mothion::ui
