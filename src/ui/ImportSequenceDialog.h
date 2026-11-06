#pragma once

#include <QDialog>

class QLabel;
class QSpinBox;

namespace mothion::ui {

// The options of an image sequence about to be imported: where it starts on the timeline and how many frames
// each image is shown for. Tells where that puts the end, and whether the scene has to grow for it.
class ImportSequenceDialog : public QDialog
{
    Q_OBJECT

public:
    ImportSequenceDialog(int images, int currentFrame, int sceneFrames, QWidget *parent = nullptr);

    // Where the first image goes, from 0.
    int startFrame() const;
    // How many frames each image is shown for.
    int hold() const;

private:
    void showEnd();

    int m_images;
    int m_sceneFrames;
    QSpinBox *m_start;
    QSpinBox *m_hold;
    QLabel *m_end;
};

} // namespace mothion::ui
