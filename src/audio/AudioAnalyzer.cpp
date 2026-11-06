#include "audio/AudioAnalyzer.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QBuffer>

#include <algorithm>
#include <cmath>

namespace mothion::audio {

namespace {

// The largest |sample| of a buffer's frames [from, to), all channels, as 0..1.
template <class Sample>
float peakOf(const Sample *samples, int channels, int from, int to, float scale)
{
    float peak = 0.0f;
    for (int i = from * channels; i < to * channels; ++i)
        peak = std::max(peak, std::abs(float(samples[i])) * scale);
    return std::min(peak, 1.0f);
}

} // namespace

AudioAnalyzer::AudioAnalyzer(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<mothion::audio::Waveform>();
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(3000);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        if (!m_decoder || m_frames > 0 || m_decoder->isDecoding())
            return;
        const void *silentKey = m_key;
        stop();
        emit failed(silentKey, tr("This audio file cannot be read."));
    });
}

AudioAnalyzer::~AudioAnalyzer()
{
    stop();
}

void AudioAnalyzer::stop()
{
    m_watchdog.stop();
    if (m_decoder) {
        m_decoder->disconnect(this);
        m_decoder->stop();
        m_decoder->deleteLater();
        m_decoder = nullptr;
    }
    delete m_buffer;
    m_buffer = nullptr;
    m_bytes.clear();
    m_data.reset();
}

void AudioAnalyzer::analyze(std::shared_ptr<const std::vector<unsigned char>> data, const void *key)
{
    stop();
    m_key = key;
    m_wave = {};
    m_frames = 0;
    m_sampleRate = 0;
    if (!data || data->empty()) {
        emit failed(key, tr("The audio file is empty."));
        return;
    }
    m_data = std::move(data);
    // The file is read straight from the project's copy (nothing is copied).
    m_bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(m_data->data()), qsizetype(m_data->size()));
    m_buffer = new QBuffer(&m_bytes);
    m_buffer->open(QIODevice::ReadOnly);
    m_decoder = new QAudioDecoder(this);
    // Connected before the source is given: some backends report a file they cannot open at once.
    connect(m_decoder, &QAudioDecoder::bufferReady, this, &AudioAnalyzer::take);
    connect(m_decoder, &QAudioDecoder::finished, this, &AudioAnalyzer::done);
    connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this](QAudioDecoder::Error) {
        const void *failedKey = m_key;
        const QString message = m_decoder ? m_decoder->errorString() : QString();
        stop();
        emit failed(failedKey, message.isEmpty() ? tr("This audio file cannot be read.") : message);
    });
    // The samples as the file has them (asking for another format made some files, mp3 among them, never end).
    m_decoder->setSourceDevice(m_buffer);
    if (!m_decoder) // an error at once has already stopped it
        return;
    m_decoder->start();
    // A file the backend cannot make anything of is not always reported: if nothing is decoding after a while,
    // it is not going to.
    m_watchdog.start();
}

void AudioAnalyzer::take()
{
    while (m_decoder && m_decoder->bufferAvailable()) {
        const QAudioBuffer buffer = m_decoder->read();
        if (!buffer.isValid())
            continue;
        const QAudioFormat format = buffer.format();
        const int channels = std::max(1, format.channelCount());
        const int rate = format.sampleRate();
        if (rate <= 0)
            continue;
        m_sampleRate = rate;
        const int frames = int(buffer.frameCount());
        const int slice = std::max(1, int(std::lround(rate * Waveform::kPeakSeconds)));
        int from = 0;
        while (from < frames) {
            // The frames up to the end of the slice the first one is in.
            const qint64 absolute = m_frames + from;
            const int to = int(std::min<qint64>(frames, from + slice - absolute % slice));
            float peak = 0.0f;
            switch (format.sampleFormat()) {
            case QAudioFormat::Int16: peak = peakOf(buffer.constData<qint16>(), channels, from, to, 1.0f / 32768.0f); break;
            case QAudioFormat::Int32: peak = peakOf(buffer.constData<qint32>(), channels, from, to, 1.0f / 2147483648.0f); break;
            case QAudioFormat::UInt8: peak = peakOf(buffer.constData<quint8>(), channels, from, to, 1.0f / 128.0f); break;
            case QAudioFormat::Float: peak = peakOf(buffer.constData<float>(), channels, from, to, 1.0f); break;
            default: break;
            }
            const std::size_t index = std::size_t(absolute / slice);
            if (m_wave.peaks.size() <= index)
                m_wave.peaks.resize(index + 1, 0.0f);
            m_wave.peaks[index] = std::max(m_wave.peaks[index], peak);
            from = to;
        }
        m_frames += frames;
    }
}

void AudioAnalyzer::done()
{
    take();
    const void *key = m_key;
    Waveform wave = std::move(m_wave);
    wave.duration = m_sampleRate > 0 ? double(m_frames) / m_sampleRate : 0.0;
    stop();
    if (wave.isEmpty() || wave.duration <= 0.0) {
        emit failed(key, tr("This audio file has no sound to read."));
        return;
    }
    emit finished(key, wave);
}

} // namespace mothion::audio
