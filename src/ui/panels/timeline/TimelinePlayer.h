#pragma once

#include <QElapsedTimer>
#include <limits>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "document/DocumentController.h"

namespace mothion::audio {
class AudioPlayer;
}


namespace mothion::ui {

// Plays the animation on the canvas: the current frame moves on at the document's frame rate, from where it is
// through the play range (the whole scene unless it is set) and round again, with the audio track (if there is one) along. The frame shown follows
// the clock, not a count of ticks: a slow redraw skips frames instead of falling behind the sound.
//
// It goes on from wherever the current frame is put (a click on the ruler, the arrow keys); it stops at a new
// project, or when stopped. Pause is the same thing as stop: the frame stays where it is.
class TimelinePlayer : public QObject
{
    Q_OBJECT

public:
    TimelinePlayer(document::DocumentController *document, audio::AudioPlayer *audio, QObject *parent = nullptr);

    bool isPlaying() const { return m_timer.isActive(); }
    void start();
    void stop();
    void toggle() { isPlaying() ? stop() : start(); }

    // The frames that play are the document's play range (see DocumentController::setPlayRange).
    static constexpr int kSceneEnd = document::DocumentController::kSceneEnd;
    void setRange(int first, int last) { m_document->setPlayRange(first, last); }
    int firstFrame() const { return m_document->playRangeFirst(); }
    int lastFrame() const { return m_document->playRangeLast(); }
    bool followsSceneEnd() const { return m_document->playRangeFollowsSceneEnd(); }

signals:
    void playingChanged(bool playing);
    void rangeChanged();

private:
    void tick();
    // The clock starts again from `frame` (as at the start, or after the frame was moved by hand).
    void restartAt(int frame);

    document::DocumentController *m_document;
    QPointer<audio::AudioPlayer> m_audio;
    QTimer m_timer;
    QElapsedTimer m_clock;
    int m_startFrame = 0;
    int m_shown = 0; // where the clock has the frame (not held at 0 before the scene starts)
    bool m_driving = false; // this is the one moving the frame
};

} // namespace mothion::ui
