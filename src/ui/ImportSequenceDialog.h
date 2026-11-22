#pragma once

#include <QDialog>

class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QRadioButton;
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

    // For a video: the frame rate its pictures are taken at, chosen at the top, each choice with how many pictures it
    // makes (the end below follows): the scene's (the default: the video keeps its timing), every frame of the video as it
    // is, or one typed. A video `duration` seconds long at `videoFps` (0 if not known: no choice of its own frames).
    void offerFrameRates(double sceneFps, double videoFps, double duration);
    // The rate chosen, frames a second; 0 for every frame of the video (and for a sequence of images).
    double frameRate() const;

private:
    void showEnd();
    void showRates();
    int imagesAt(double fps) const;

    int m_images;
    int m_sceneFrames;
    QSpinBox *m_start;
    QSpinBox *m_hold;
    QLabel *m_end;
    QLabel *m_count;
    QFormLayout *m_form;
    // The frame rates of a video (none for images).
    double m_sceneFps = 0, m_videoFps = 0, m_duration = 0;
    QRadioButton *m_sceneRate = nullptr, *m_everyFrame = nullptr, *m_customRate = nullptr;
    QDoubleSpinBox *m_custom = nullptr;
};

} // namespace mothion::ui
