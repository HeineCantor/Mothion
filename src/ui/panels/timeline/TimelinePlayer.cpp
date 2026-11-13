#include "ui/panels/timeline/TimelinePlayer.h"

#include "audio/AudioPlayer.h"
#include "document/DocumentController.h"

#include <algorithm>
#include <cmath>

namespace mothion::ui {

TimelinePlayer::TimelinePlayer(document::DocumentController *document, audio::AudioPlayer *audio, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_audio(audio)
{
    connect(&m_timer, &QTimer::timeout, this, &TimelinePlayer::tick);
    // The frame moved by someone else while it plays (a click on the ruler): it plays on from there.
    connect(m_document, &document::DocumentController::currentFrameChanged, this, [this](int frame) {
        if (isPlaying() && !m_driving)
            restartAt(frame);
    });
    connect(m_document, &document::DocumentController::documentReplaced, this, &TimelinePlayer::stop);
    connect(m_document, &document::DocumentController::playRangeChanged, this, [this] {
        if (isPlaying())
            restartAt(m_document->currentFrame());
        emit rangeChanged();
    });
}

void TimelinePlayer::start()
{
    if (isPlaying())
        return;
    const double fps = std::max(1.0, m_document->document().frameRate().fps());
    // Checked more often than a frame lasts, so each is shown near its time.
    m_timer.setInterval(std::max(1, int(1000.0 / fps / 3.0)));
    m_timer.start();
    if (m_audio)
        m_audio->setRate(1.0);
    // From where the frame is, or from the start of the range if it is out of it.
    int frame = m_document->currentFrame();
    if (frame < firstFrame() || frame > lastFrame()) {
        frame = firstFrame();
        m_driving = true;
        m_document->setCurrentFrame(frame); // (held at 0 if the range starts before the scene)
        m_driving = false;
    }
    restartAt(frame);
    emit playingChanged(true);
}

void TimelinePlayer::stop()
{
    if (!isPlaying())
        return;
    m_timer.stop();
    if (m_audio)
        m_audio->stop();
    emit playingChanged(false);
}

void TimelinePlayer::restartAt(int frame)
{
    m_startFrame = frame;
    m_shown = frame;
    m_clock.start();
    if (m_audio)
        m_audio->play(std::max(0, frame));
}

void TimelinePlayer::tick()
{
    const int first = firstFrame(), length = lastFrame() - first + 1;
    const double fps = std::max(1.0, m_document->document().frameRate().fps());
    // The frame the clock says: the start, plus the frames that have gone by, round the scene.
    const qint64 passed = qint64(double(m_clock.elapsed()) * fps / 1000.0);
    // Where the clock started, in the range (a start out of it, as a click on the ruler past its end, counts as its first).
    const int offset = m_startFrame >= first && m_startFrame < first + length ? m_startFrame - first : 0;
    const int frame = first + int((offset + passed) % length);
    if (frame == m_shown)
        return;
    const bool wrapped = frame < m_shown;
    m_shown = frame;
    m_driving = true;
    m_document->setCurrentFrame(std::max(0, frame));
    m_driving = false;
    if (!m_audio)
        return;
    if (wrapped) { // round again: the sound from the start too
        m_startFrame = frame;
        m_clock.start();
        m_audio->play(std::max(0, frame));
    } else {
        m_audio->resync(std::max(0, frame));
    }
}

} // namespace mothion::ui
