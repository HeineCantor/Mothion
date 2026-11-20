#pragma once

#include "core/document/Audio.h"
#include "io/LipSyncJob.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QAudioDecoder;
class QBuffer;
class QThread;

namespace mothion::io {

// Lip sync from the sound with ZIPA (Zhu et al., "ZIPA: A family of efficient models for multilingual phone
// recognition", ACL 2025; CC BY 4.0): a phone recognizer of any language, run inside Mothion with ONNX Runtime. It
// hears the phones (IPA) said and when; doc::mouthCuesFromPhones makes them mouths.
//
// The model is ZIPA-CR small, int8 (model.int8.onnx, with its phones in tokens.txt): log mel filter banks in (Fbank.h),
// for each 20 ms the likelihood of each phone out (CTC).

// Whether this Mothion was built with ONNX Runtime (without it there is no ZIPA, only Rhubarb).
bool zipaBuiltIn();

// The folder of the model (model.int8.onnx and tokens.txt): the one named by MOTHION_ZIPA, then the one that comes with
// Mothion ("zipa" beside it), then Mothion's own folder. Empty if there is none.
QString findZipaModel();

// The phones of tokens.txt ("<phone> <id>" a line), by their id; empty if it is not that.
std::vector<std::string> parseZipaTokens(const QByteArray &text);

// The recognizer itself, synchronous (the job below runs it in a thread of its own).
class ZipaRecognizer
{
public:
    ZipaRecognizer();
    ~ZipaRecognizer();

    // Loads the model of `modelDir`; false, with `error`, if it cannot (or Mothion has no ONNX Runtime).
    bool load(const QString &modelDir, QString *error);

    // The phones in `samples` (16 kHz mono, -1..1), heard 20 seconds at a time. After each, `progress` is told how far
    // it is (0..1) and says whether to go on; nothing (and no error) if it said not to.
    std::optional<std::vector<doc::TimedPhone>> recognize(const std::vector<float> &samples,
                                                          const std::function<bool(double)> &progress, QString *error);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// One run of ZIPA on an audio track, in the background: the sound decoded (whatever the platform reads) and made 16 kHz
// mono, the phones heard in a thread of their own, made mouths (with the options' extended shapes) and given at the end.
class ZipaJob : public LipSyncJob
{
    Q_OBJECT

public:
    explicit ZipaJob(QObject *parent = nullptr);
    ~ZipaJob() override;

    // False, with `reason`, if it cannot begin (no ONNX Runtime, no model, no sound). `modelDir`: findZipaModel() if
    // empty.
    bool start(const doc::AudioTrack &track, const LipSyncOptions &options, QString *reason, const QString &modelDir = {});
    void cancel() override;
    bool isRunning() const override;

    // The phones heard by the last run that finished (for a look at what it heard).
    const std::vector<doc::TimedPhone> &phones() const { return m_phones; }

private:
    void takeAudio();
    void audioDecoded();
    void stopDecoding();
    void fail(const QString &reason);

    QString m_modelDir;
    LipSyncOptions m_options;
    std::shared_ptr<const std::vector<unsigned char>> m_data;
    QByteArray m_bytes;
    QAudioDecoder *m_decoder = nullptr;
    QBuffer *m_buffer = nullptr;
    std::vector<float> m_samples; // mono, as decoded
    int m_sampleRate = 0;
    QThread *m_thread = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    std::vector<doc::TimedPhone> m_phones;
};

} // namespace mothion::io
