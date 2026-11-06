#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

class QAudioOutput;
class QMediaPlayer;

namespace mothion::document {
class DocumentController;
}

namespace mothion::audio {

// Plays the project's audio track (document::DocumentController::audioTrack()) against the frames: from a frame
// on, as the animation plays, or a short burst of the sound at a frame, to hear it while the timeline is
// stepped or scrubbed. The sound is read from the project's own copy of the file; it follows the track as it
// changes (a new song, the volume, mute).
//
// The frames and the sound are not locked together: the one that plays the frames (the Autoplay window)
// tells here which frame it is at, and the sound is put back where it should be if it has drifted.
class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayer(document::DocumentController *document, QObject *parent = nullptr);
    ~AudioPlayer() override;

    // Where the sound is at `frame`: seconds from its start, negative before it begins.
    static double secondsAt(int frame, int offsetFrames, double fps);

    // There is a sound that would be heard: a track, not muted (in the project, or here: see setSilenced()), not
    // at volume 0.
    bool isAudible() const;
    // Off for this session only, whatever the track says (the Autoplay window's sound button): the project is
    // not changed. Silencing stops the sound that is playing.
    void setSilenced(bool silenced);
    bool isSilenced() const { return m_silenced; }
    // How fast it plays, as a multiple of its own speed (0.5: half, lower, slower); 1 is as it was recorded.
    void setRate(double rate);
    double rate() const;
    // The sound is sounding, from a frame (see play()).
    bool isPlaying() const { return m_playing; }
    // Playing was asked for and not stopped, whether or not it is heard now (muted, silenced): when it can be
    // heard again, resync() takes it up at the frame the animation is at.
    bool isRequested() const { return m_requested; }

    // The sound from `frame` on, as long as it is not stopped: it starts at once when it has begun by then, or
    // when the frames get to where it begins.
    void play(int frame);
    // The frame the animation is at now: the sound is moved if it is more than `toleranceMs` from it.
    // Not in the first moments after play(): the backend takes a few hundred ms to start, and putting it back
    // then would only start it over.
    void resync(int frame, int toleranceMs = 250);
    void stop();
    // A short burst of the sound at `frame` (a stop in the timeline, a step), unless it is playing.
    void scrub(int frame);

signals:
    void silencedChanged(bool silenced);

private:
    void sync();
    // Quiet, but still wanted: see isRequested().
    void pauseSound();
    double fps() const;
    // The position of the sound in the player, in ms: -1 when it is not known.
    qint64 positionMs() const;

    document::DocumentController *m_document;
    QMediaPlayer *m_player;
    QAudioOutput *m_output;
    QBuffer *m_buffer = nullptr;
    QByteArray m_bytes;
    std::shared_ptr<const std::vector<unsigned char>> m_data;
    const void *m_key = nullptr;
    bool m_playing = false;
    bool m_requested = false;
    bool m_silenced = false;
    QElapsedTimer m_started; // since the sound was last started at a frame
    QTimer m_delay; // plays from the start when the frames get to where the sound begins
    QTimer m_burst; // ends a scrub's burst
};

} // namespace mothion::audio
