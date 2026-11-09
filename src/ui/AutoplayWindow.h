#pragma once

#include "canvas/FrameRenderer.h"
#include "core/document/Ids.h"

#include <QPointer>
#include <QTimer>
#include "core/document/CelMotion.h"

#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

namespace mothion::audio {
class AudioPlayer;
}
namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// A window playing the animation in a loop, frame by frame, as the canvas
// shows it (the visible layers over the paper), from the first frame to
// the last one anything is exposed on, or only over a range of them: two
// handles under the slider (double click to bring them back out) narrow the
// loop to the frames between them. "Play" starts and stops it; the
// slider under the picture follows the playback and, dragged, picks the
// frame; "Flip" shows it mirrored left to right; "Go to frame" sets the timeline's current time slot to the one shown; the speed is chosen in frames per second (the document's rate to
// begin with). A sound button turns the audio track on and off (for this window; the project is left as it is).
// Space plays and stops too. It plays on its own, apart from
// the canvas's current frame, and follows the document as it is edited.
class AutoplayWindow : public QWidget
{
    Q_OBJECT

public:
    // With `audio`, the project's audio track plays with the frames (when the speed is the document's own: the
    // sound is not stretched), and is heard in short bursts when the slider is dragged.
    explicit AutoplayWindow(document::DocumentController *document, audio::AudioPlayer *audio = nullptr,
                            QWidget *parent = nullptr);
    ~AutoplayWindow() override;

    int frame() const { return m_frame; }
    int frameCount() const { return m_frameCount; } // of the loop
    // The frames looped, first and last (all of them unless narrowed).
    int loopStart() const;
    int loopEnd() const;
    void setLoopRange(int first, int last);
    bool isPlaying() const;
    double fps() const;
    const QImage &image() const { return m_renderer.image(); } // the frame shown

    static const QList<double> &speeds(); // the choices, in fps

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showFrame(int frame);
    void advance();
    void documentChanged();
    void updateRange();
    void updateLoopBar();
    void updateTimer();
    // The sound plays while the frames do, as fast as they go (slower or faster than the document's rate, it goes
    // slower or faster with them).
    void syncAudio();
    void updateSoundButton();
    void updatePlayButton();
    // What a frame shows: each layer's drawing, if visible. Frames that show
    // the same are not composed again.
    std::vector<doc::DrawingId> exposedAt(int frame) const;
    std::vector<doc::CelPose> posesAt(int frame) const;

    document::DocumentController *m_document;
    QPointer<audio::AudioPlayer> m_audio; // may go before the window (both are the main window's children)
    canvas::FrameRenderer m_renderer;
    QImage m_output; // the frame seen through the camera, when the project has one
    QWidget *m_picture;
    QSlider *m_slider;
    QLabel *m_position;
    QWidget *m_loopBar;
    QLabel *m_loopLabel;
    QPushButton *m_play; // checked while playing: then it offers Stop
    QPushButton *m_goTo;
    QPushButton *m_flip; // mirrors the picture
    QPushButton *m_sound; // the audio track on or off, here only (checked: it plays)
    QComboBox *m_speed;
    QTimer m_timer;
    QTimer m_refresh; // edits come in bursts (a stroke): composed again once after them
    int m_frame = 0;
    int m_frameCount = 1;
    int m_loopFirst = 0;
    int m_loopLast = -1; // -1: to the end, whatever it becomes
    std::vector<doc::DrawingId> m_shown; // what the composed image shows
    std::vector<doc::CelPose> m_shownPoses; // and where the cels are moved to (a held drawing moves on its keys)
    bool m_stale = true;                 // the document changed since it was composed
};

} // namespace mothion::ui
