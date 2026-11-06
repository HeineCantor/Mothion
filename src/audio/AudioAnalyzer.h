#pragma once

#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

class QAudioDecoder;
class QBuffer;

namespace mothion::audio {

// What decoding a sound file gives for drawing it: its length, and its loudness as one peak (0..1, the
// largest sample of the slice, all channels) for each slice of `kPeakSeconds`.
struct Waveform
{
    static constexpr double kPeakSeconds = 0.01;
    double duration = 0.0; // seconds
    std::vector<float> peaks;

    bool isEmpty() const { return peaks.empty(); }
};

// Decodes a sound file (mp3, wav, ogg, flac... whatever the platform's backend reads) into a Waveform, in the
// background of the event loop: nothing blocks while a song is read. One at a time; a new request drops the
// one running.
class AudioAnalyzer : public QObject
{
    Q_OBJECT

public:
    explicit AudioAnalyzer(QObject *parent = nullptr);
    ~AudioAnalyzer() override;

    // `data` is the file, as it is stored in the project. `key` is handed back with the result, to tell which
    // request it answers.
    void analyze(std::shared_ptr<const std::vector<unsigned char>> data, const void *key);
    bool isRunning() const { return m_decoder != nullptr; }

signals:
    void finished(const void *key, const mothion::audio::Waveform &waveform);
    void failed(const void *key, const QString &reason);

private:
    void take();
    void done();
    void stop();

    QTimer m_watchdog;
    QAudioDecoder *m_decoder = nullptr;
    QBuffer *m_buffer = nullptr;
    QByteArray m_bytes;
    std::shared_ptr<const std::vector<unsigned char>> m_data;
    const void *m_key = nullptr;
    Waveform m_wave;
    qint64 m_frames = 0; // decoded so far
    int m_sampleRate = 0;
};

} // namespace mothion::audio

Q_DECLARE_METATYPE(mothion::audio::Waveform)
