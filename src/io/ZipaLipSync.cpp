#include "io/ZipaLipSync.h"

#include "io/Fbank.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <stdexcept>

#ifdef MOTHION_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace mothion::io {

namespace {

const QString kModelFile = QStringLiteral("model.int8.onnx");
const QString kTokensFile = QStringLiteral("tokens.txt");
constexpr double kChunkSeconds = 20.0;   // heard this much at a time: the attention grows with the square of the length
constexpr double kOutputSeconds = 0.02;  // the model gives a frame every two of the features'
constexpr double kOutputOffset = 0.03;   // where its first frame is centred, about

bool hasModel(const QString &dir)
{
    return !dir.isEmpty() && QFileInfo::exists(QDir(dir).filePath(kModelFile))
           && QFileInfo::exists(QDir(dir).filePath(kTokensFile));
}

} // namespace

bool zipaBuiltIn()
{
#ifdef MOTHION_HAVE_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

QString findZipaModel()
{
    if (const QString chosen = qEnvironmentVariable("MOTHION_ZIPA"); hasModel(chosen))
        return chosen;
    const QString here = QCoreApplication::applicationDirPath();
    for (const QString &dir : {QDir(here).filePath(QStringLiteral("zipa")), here})
        if (hasModel(dir))
            return dir;
    return {};
}

std::vector<std::string> parseZipaTokens(const QByteArray &text)
{
    std::vector<std::string> tokens;
    for (const QByteArray &raw : text.split('\n')) {
        const QByteArray line = raw.endsWith('\r') ? raw.chopped(1) : raw;
        const qsizetype space = line.lastIndexOf(' ');
        if (space <= 0)
            continue;
        bool ok = false;
        const int id = line.mid(space + 1).toInt(&ok);
        if (!ok || id < 0 || id > 100000)
            return {};
        if (tokens.size() <= std::size_t(id))
            tokens.resize(std::size_t(id) + 1);
        tokens[std::size_t(id)] = line.left(space).toStdString();
    }
    return tokens;
}

// ---------------------------------------------------------------------------------------------------------------------

struct ZipaRecognizer::Impl
{
    std::vector<std::string> tokens;
#ifdef MOTHION_HAVE_ONNXRUNTIME
    std::unique_ptr<Ort::Session> session;
#endif
};

ZipaRecognizer::ZipaRecognizer()
    : d(std::make_unique<Impl>())
{
}

ZipaRecognizer::~ZipaRecognizer() = default;

bool ZipaRecognizer::load(const QString &modelDir, QString *error)
{
    auto refuse = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    QFile tokensFile(QDir(modelDir).filePath(kTokensFile));
    if (!tokensFile.open(QIODevice::ReadOnly))
        return refuse(QCoreApplication::translate("ZipaJob", "The phones of ZIPA (%1) cannot be read").arg(tokensFile.fileName()));
    d->tokens = parseZipaTokens(tokensFile.readAll());
    if (d->tokens.empty())
        return refuse(QCoreApplication::translate("ZipaJob", "The phones of ZIPA (%1) are not what was expected").arg(tokensFile.fileName()));
#ifdef MOTHION_HAVE_ONNXRUNTIME
    try {
        static std::once_flag api;
        std::call_once(api, [] { Ort::InitApi(); }); // (ORT_API_MANUAL_INIT: not as the program starts)
        static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "mothion");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const QString model = QDir(modelDir).filePath(kModelFile);
#ifdef _WIN32
        const std::wstring path = model.toStdWString();
#else
        const std::string path = QFile::encodeName(model).toStdString();
#endif
        d->session = std::make_unique<Ort::Session>(env, path.c_str(), options);
    } catch (const Ort::Exception &e) {
        return refuse(QCoreApplication::translate("ZipaJob", "The ZIPA model cannot be loaded: %1").arg(QString::fromUtf8(e.what())));
    }
    return true;
#else
    return refuse(QCoreApplication::translate("ZipaJob", "This Mothion was built without ONNX Runtime: ZIPA cannot run"));
#endif
}

std::optional<std::vector<doc::TimedPhone>> ZipaRecognizer::recognize(const std::vector<float> &samples,
                                                                      const std::function<bool(double)> &progress,
                                                                      QString *error)
{
    std::vector<doc::TimedPhone> phones;
#ifdef MOTHION_HAVE_ONNXRUNTIME
    if (!d->session) {
        if (error)
            *error = QCoreApplication::translate("ZipaJob", "The ZIPA model is not loaded");
        return std::nullopt;
    }
    const auto chunk = std::size_t(kChunkSeconds * kFbankRate);
    const std::size_t total = samples.size();
    try {
        const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const char *inputNames[] = {"x", "x_lens"};
        const char *outputNames[] = {"log_probs"};
        for (std::size_t from = 0; from < total; from += chunk) {
            const std::size_t count = std::min(chunk, total - from);
            int frames = 0;
            std::vector<float> features = kaldiFbank(samples.data() + from, count, &frames);
            if (frames >= 16) { // shorter is too short to say anything (and for the model's first layers)
                const std::array<int64_t, 3> shape{1, frames, kFbankBins};
                const std::array<int64_t, 1> lengthShape{1};
                int64_t length = frames;
                Ort::Value inputs[] = {
                    Ort::Value::CreateTensor<float>(memory, features.data(), features.size(), shape.data(), shape.size()),
                    Ort::Value::CreateTensor<int64_t>(memory, &length, 1, lengthShape.data(), lengthShape.size()),
                };
                auto outputs = d->session->Run(Ort::RunOptions{nullptr}, inputNames, inputs, 2, outputNames, 1);
                const auto outShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
                if (outShape.size() != 3 || outShape[2] != int64_t(d->tokens.size()))
                    throw std::runtime_error("the model's phones are not those of tokens.txt");
                const double offset = double(from) / kFbankRate;
                for (doc::TimedPhone &p : doc::ctcPhones(outputs[0].GetTensorData<float>(), int(outShape[1]), d->tokens,
                                                         kOutputSeconds, offset + kOutputOffset))
                    phones.push_back(std::move(p));
            }
            if (progress && !progress(double(from + count) / double(total)))
                return std::nullopt;
        }
    } catch (const std::exception &e) {
        if (error)
            *error = QCoreApplication::translate("ZipaJob", "ZIPA could not hear the sound: %1").arg(QString::fromUtf8(e.what()));
        return std::nullopt;
    }
    return phones;
#else
    Q_UNUSED(samples)
    Q_UNUSED(progress)
    if (error)
        *error = QCoreApplication::translate("ZipaJob", "This Mothion was built without ONNX Runtime: ZIPA cannot run");
    return std::nullopt;
#endif
}

// ---------------------------------------------------------------------------------------------------------------------

namespace {

// The mean of a buffer's channels, appended to `out`.
template <typename T>
void appendMono(const QAudioBuffer &buffer, float scale, float bias, std::vector<float> &out)
{
    const int channels = std::max(1, buffer.format().channelCount());
    const qsizetype frames = buffer.frameCount();
    const T *data = buffer.constData<T>();
    for (qsizetype f = 0; f < frames; ++f) {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
            sum += float(data[f * channels + c]) * scale + bias;
        out.push_back(sum / float(channels));
    }
}

} // namespace

ZipaJob::ZipaJob(QObject *parent)
    : LipSyncJob(parent)
{
}

ZipaJob::~ZipaJob() { cancel(); }

bool ZipaJob::isRunning() const { return m_decoder || m_thread; }

bool ZipaJob::start(const doc::AudioTrack &track, const LipSyncOptions &options, QString *reason, const QString &modelDir)
{
    auto refuse = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    cancel();
    if (!zipaBuiltIn())
        return refuse(tr("This Mothion was built without ONNX Runtime, so ZIPA cannot run: choose Rhubarb Lip Sync."));
    m_modelDir = modelDir.isEmpty() ? findZipaModel() : modelDir;
    if (!hasModel(m_modelDir))
        return refuse(tr("The ZIPA model was not found. It comes with Mothion's releases; otherwise put model.int8.onnx "
                         "and tokens.txt in a folder named \"zipa\" beside Mothion, or set MOTHION_ZIPA to their folder."));
    if (!track.data || track.data->empty())
        return refuse(tr("The project has no audio to hear"));
    m_options = options;
    m_data = track.data;
    m_samples.clear();
    m_sampleRate = 0;
    m_phones.clear();
    // The file read straight from the project's copy; its samples as the file has them (asking the decoder for another
    // format makes some files never end), made 16 kHz mono afterwards.
    m_bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(m_data->data()), qsizetype(m_data->size()));
    m_buffer = new QBuffer(&m_bytes, this);
    m_buffer->open(QIODevice::ReadOnly);
    m_decoder = new QAudioDecoder(this);
    connect(m_decoder, &QAudioDecoder::bufferReady, this, &ZipaJob::takeAudio);
    connect(m_decoder, &QAudioDecoder::finished, this, &ZipaJob::audioDecoded);
    connect(m_decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, [this](QAudioDecoder::Error) {
        const QString message = m_decoder ? m_decoder->errorString() : QString();
        fail(message.isEmpty() ? tr("The audio cannot be read") : tr("The audio cannot be read: %1").arg(message));
    });
    m_decoder->setSourceDevice(m_buffer);
    if (!m_decoder) // an error at once has already failed it
        return true;
    m_decoder->start();
    // A file the backend makes nothing of is not always reported: if nothing came after a while, nothing will.
    QTimer::singleShot(15000, this, [this, decoder = QPointer<QAudioDecoder>(m_decoder)] {
        if (decoder && decoder == m_decoder && m_samples.empty())
            fail(tr("The audio cannot be read"));
    });
    return true;
}

void ZipaJob::takeAudio()
{
    while (m_decoder && m_decoder->bufferAvailable()) {
        const QAudioBuffer buffer = m_decoder->read();
        if (!buffer.isValid() || buffer.format().sampleRate() <= 0)
            continue;
        m_sampleRate = buffer.format().sampleRate();
        switch (buffer.format().sampleFormat()) {
        case QAudioFormat::Int16: appendMono<qint16>(buffer, 1.0f / 32768.0f, 0.0f, m_samples); break;
        case QAudioFormat::Int32: appendMono<qint32>(buffer, 1.0f / 2147483648.0f, 0.0f, m_samples); break;
        case QAudioFormat::UInt8: appendMono<quint8>(buffer, 1.0f / 128.0f, -1.0f, m_samples); break;
        case QAudioFormat::Float: appendMono<float>(buffer, 1.0f, 0.0f, m_samples); break;
        default: break;
        }
    }
    emit progress(0.0);
}

void ZipaJob::stopDecoding()
{
    if (m_decoder) {
        m_decoder->disconnect(this);
        m_decoder->stop();
        m_decoder->deleteLater();
        m_decoder = nullptr;
    }
    if (m_buffer) {
        m_buffer->deleteLater();
        m_buffer = nullptr;
    }
}

void ZipaJob::audioDecoded()
{
    takeAudio();
    stopDecoding();
    if (m_samples.empty() || m_sampleRate <= 0) {
        fail(tr("The audio has no sound to hear"));
        return;
    }
    // The phones heard in a thread of their own; what it finds comes back by the event loop.
    auto samples = std::make_shared<std::vector<float>>(resampled(m_samples, m_sampleRate, kFbankRate));
    m_samples.clear();
    m_samples.shrink_to_fit();
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cancelled;
    const QString modelDir = m_modelDir;
    const std::string shapes = m_options.extendedShapes.toStdString();
    m_thread = QThread::create([this, samples, cancelled, modelDir, shapes] {
        ZipaRecognizer recognizer;
        QString error;
        std::optional<std::vector<doc::TimedPhone>> phones;
        if (recognizer.load(modelDir, &error))
            phones = recognizer.recognize(*samples, [this, cancelled](double fraction) {
                QMetaObject::invokeMethod(this, [this, fraction, cancelled] {
                    if (!*cancelled)
                        emit progress(fraction);
                }, Qt::QueuedConnection);
                return !*cancelled;
            }, &error);
        if (*cancelled)
            return;
        const double duration = double(samples->size()) / kFbankRate;
        QMetaObject::invokeMethod(this, [this, phones = std::move(phones), error, duration, shapes, cancelled] {
            if (*cancelled) // cancelled after it was sent
                return;
            if (m_thread) {
                m_thread->wait();
                m_thread->deleteLater();
                m_thread = nullptr;
            }
            if (!phones) {
                emit failed(error);
                return;
            }
            m_phones = *phones;
            emit finished(doc::mouthCuesFromPhones(m_phones, duration, shapes));
        }, Qt::QueuedConnection);
    });
    m_thread->start();
}

void ZipaJob::fail(const QString &reason)
{
    stopDecoding();
    m_samples.clear();
    emit failed(reason);
}

void ZipaJob::cancel()
{
    stopDecoding();
    m_samples.clear();
    if (m_thread) {
        *m_cancelled = true;
        m_thread->wait(); // at most one piece of the sound more
        delete m_thread;
        m_thread = nullptr;
    }
}

} // namespace mothion::io
