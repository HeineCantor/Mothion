#include "audio/AudioPlayer.h"

#include "document/DocumentController.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace mothion::audio {

namespace {
constexpr int kBurstMs = 110;    // how long a scrub is heard
constexpr int kWarmUpMs = 800;   // after a start, before the sound is checked against the frames
}

AudioPlayer::AudioPlayer(document::DocumentController *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_player(new QMediaPlayer(this))
    , m_output(new QAudioOutput(this))
{
    m_player->setAudioOutput(m_output);
    m_delay.setSingleShot(true);
    m_burst.setSingleShot(true);
    connect(&m_delay, &QTimer::timeout, this, [this] {
        if (m_playing)
            m_player->play();
    });
    connect(&m_burst, &QTimer::timeout, this, [this] {
        if (!m_playing)
            m_player->pause();
    });
    connect(m_document, &document::DocumentController::structureChanged, this, &AudioPlayer::sync);
    connect(m_document, &document::DocumentController::documentReplaced, this, &AudioPlayer::sync);
    sync();
}

AudioPlayer::~AudioPlayer()
{
    m_player->stop();
    m_player->setSourceDevice(nullptr); // before the buffer it reads goes
}

double AudioPlayer::secondsAt(int frame, int offsetFrames, double fps)
{
    return fps > 0.0 ? double(frame - offsetFrames) / fps : 0.0;
}

double AudioPlayer::fps() const
{
    return m_document->document().frameRate().fps();
}

bool AudioPlayer::isAudible() const
{
    const auto &track = m_document->audioTrack();
    return track && track->data && !track->muted && track->volume > 0.0f;
}

void AudioPlayer::sync()
{
    const auto &track = m_document->audioTrack();
    const void *key = track && track->data ? track->data.get() : nullptr;
    if (key != m_key) { // another sound, or none
        stop();
        m_key = key;
        m_player->setSourceDevice(nullptr);
        delete m_buffer;
        m_buffer = nullptr;
        m_data = track ? track->data : nullptr;
        if (key) {
            // Read from the project's copy; the name's extension tells the backend what the file is.
            m_bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(m_data->data()), qsizetype(m_data->size()));
            m_buffer = new QBuffer(&m_bytes, this);
            m_buffer->open(QIODevice::ReadOnly);
            m_player->setSourceDevice(m_buffer, QUrl::fromLocalFile(QStringLiteral("/sound.") + QString::fromStdString(track->format)));
        }
    }
    if (track) {
        m_output->setVolume(track->volume);
        m_output->setMuted(track->muted);
        if (track->muted || track->volume <= 0.0f)
            stop();
    }
}

qint64 AudioPlayer::positionMs() const
{
    return m_player->position();
}

void AudioPlayer::play(int frame)
{
    m_delay.stop();
    m_burst.stop();
    m_playing = false;
    if (!isAudible()) {
        m_player->pause();
        return;
    }
    const auto &track = m_document->audioTrack();
    const double seconds = secondsAt(frame, track->offset, fps());
    m_playing = true;
    if (seconds < 0.0) {
        // Not begun yet: waits, at its start, for the frames to get there.
        m_player->pause();
        m_player->setPosition(0);
        m_delay.start(int(std::lround(-seconds * 1000.0)));
        return;
    }
    if (track->duration > 0.0 && seconds >= track->duration) {
        m_player->pause(); // over: silent for the rest of the loop
        return;
    }
    m_player->setPosition(qint64(std::lround(seconds * 1000.0)));
    m_player->play();
    m_started.start();
}

void AudioPlayer::resync(int frame, int toleranceMs)
{
    if (!m_playing || (m_started.isValid() && m_started.elapsed() < kWarmUpMs))
        return;
    const auto &track = m_document->audioTrack();
    if (!track)
        return;
    const double seconds = secondsAt(frame, track->offset, fps());
    if (seconds < 0.0 || (track->duration > 0.0 && seconds >= track->duration))
        return; // still to begin (the delay is waiting), or over
    if (m_player->playbackState() != QMediaPlayer::PlayingState || std::abs(double(positionMs()) - seconds * 1000.0) > toleranceMs)
        play(frame);
}

void AudioPlayer::stop()
{
    m_delay.stop();
    m_burst.stop();
    m_playing = false;
    m_player->pause();
}

void AudioPlayer::scrub(int frame)
{
    if (m_playing || !isAudible())
        return;
    const auto &track = m_document->audioTrack();
    const double seconds = secondsAt(frame, track->offset, fps());
    if (seconds < 0.0 || (track->duration > 0.0 && seconds >= track->duration))
        return;
    m_player->setPosition(qint64(std::lround(seconds * 1000.0)));
    m_player->play();
    m_burst.start(kBurstMs);
}

} // namespace mothion::audio
