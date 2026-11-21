// Tests for the lip sync from phones: the CTC output read as phones, phones made mouths, the filter banks ZIPA hears
// with (against lhotse's, which it was trained on), the resampling, and ZIPA itself when its model is there.

#include "core/document/LipSync.h"
#include "io/Fbank.h"
#include "io/ZipaLipSync.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QtEndian>

#include <cmath>
#include <numbers>

using namespace mothion;

namespace {

// A WAV file (16-bit PCM) of `samples`, each channel the same.
std::vector<unsigned char> wavOf(const std::vector<float> &samples, int rate, int channels)
{
    QByteArray pcm;
    for (const float s : samples)
        for (int c = 0; c < channels; ++c) {
            const auto v = qToLittleEndian(qint16(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f)));
            pcm.append(reinterpret_cast<const char *>(&v), 2);
        }
    QByteArray wav;
    auto u32 = [&](quint32 v) { v = qToLittleEndian(v); wav.append(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](quint16 v) { v = qToLittleEndian(v); wav.append(reinterpret_cast<const char *>(&v), 2); };
    wav.append("RIFF");
    u32(quint32(36 + pcm.size()));
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(quint16(channels));
    u32(quint32(rate));
    u32(quint32(rate * channels * 2));
    u16(quint16(channels * 2));
    u16(16);
    wav.append("data");
    u32(quint32(pcm.size()));
    wav.append(pcm);
    return {wav.begin(), wav.end()};
}

// The signal the reference filter banks were computed on (with lhotse 1.x, FbankConfig(num_filters=80, dither=0,
// snip_edges=False)).
std::vector<float> referenceSignal(int n)
{
    std::vector<float> x(std::size_t(n), 0.0f);
    for (int i = 0; i < n; ++i)
        x[std::size_t(i)] = float(0.5 * std::sin(2 * std::numbers::pi * 440 * i / 16000.0)
                                  + 0.3 * std::sin(2 * std::numbers::pi * 1234.5 * i / 16000.0)
                                  + 0.05 * (((i * 7919) % 1000) / 500.0 - 1));
    return x;
}

} // namespace

class TestLipSync : public QObject
{
    Q_OBJECT

private slots:
    void phonesHaveTheirMouths()
    {
        auto shape = [](const char *phone) { return doc::mouthShapeOfPhone(phone); };
        QCOMPARE(shape("p"), std::optional<char>('A'));
        QCOMPARE(shape("m"), std::optional<char>('A'));
        QCOMPARE(shape("f"), std::optional<char>('G'));
        QCOMPARE(shape("v"), std::optional<char>('G'));
        QCOMPARE(shape("l"), std::optional<char>('H'));
        QCOMPARE(shape("u"), std::optional<char>('F'));
        QCOMPARE(shape("w"), std::optional<char>('F'));
        QCOMPARE(shape("o"), std::optional<char>('E'));
        QCOMPARE(shape("ɔ"), std::optional<char>('E'));
        QCOMPARE(shape("a"), std::optional<char>('D'));
        QCOMPARE(shape("aː"), std::optional<char>('D')); // its marks do not count
        QCOMPARE(shape("e"), std::optional<char>('C'));
        QCOMPARE(shape("ɛ̃"), std::optional<char>('C'));
        QCOMPARE(shape("ə"), std::optional<char>('C'));
        QCOMPARE(shape("t"), std::optional<char>('B'));
        QCOMPARE(shape("i"), std::optional<char>('B'));
        QCOMPARE(shape("ʃ"), std::optional<char>('B'));
        QCOMPARE(shape("tʃ"), std::optional<char>('B'));
        QCOMPARE(shape("̃"), std::nullopt); // a nasalization alone
        QCOMPARE(shape("ː"), std::nullopt);
        QCOMPARE(shape(""), std::nullopt);
    }

    // Greedy CTC with times: a phone from the first frame of its run to the next phone, or a little past its run before
    // a pause; the blank, the word boundary and the special tokens are no phones; the same phone twice, with a blank
    // between, is two.
    void ctcOutputIsReadAsPhones()
    {
        const std::vector<std::string> tokens{"<blk>", "<unk>", "a", "m", "▁"};
        const std::vector<int> best{0, 3, 3, 0, 2, 2, 2, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 1, 0};
        std::vector<float> logProbs(best.size() * tokens.size(), -10.0f);
        for (std::size_t t = 0; t < best.size(); ++t)
            logProbs[t * tokens.size() + std::size_t(best[t])] = 0.0f;
        const auto phones = doc::ctcPhones(logProbs.data(), int(best.size()), tokens, 0.02, 0.5);
        QCOMPARE(phones.size(), std::size_t(3));
        QCOMPARE(phones[0].phone, std::string("m"));
        QCOMPARE(phones[0].start, 0.52);
        QCOMPARE(phones[0].end, 0.58); // to the next phone
        QCOMPARE(phones[1].phone, std::string("a"));
        QCOMPARE(phones[1].start, 0.58);
        QCOMPARE(phones[1].end, 0.66);
        QCOMPARE(phones[2].phone, std::string("a"));
        QCOMPARE(phones[2].start, 0.66);
        QVERIFY(std::abs(phones[2].end - (0.68 + 0.12)) < 1e-9); // a pause follows: a little past its run
    }

    void phonesMakeMouths()
    {
        const std::vector<doc::TimedPhone> phones{
            {"m", 0.1, 0.2}, {"a", 0.2, 0.4}, {"m", 0.45, 0.5}, {"f", 1.0, 1.1}, {"̃", 1.1, 1.2}};
        auto cues = doc::mouthCuesFromPhones(phones, 1.5);
        auto text = [](const std::vector<doc::MouthCue> &cues) {
            QStringList out;
            for (const auto &c : cues)
                out << QStringLiteral("%1 %2-%3").arg(QChar(c.shape)).arg(c.start).arg(c.end);
            return out.join(QStringLiteral(", "));
        };
        // The pause of 0.05 s is too short to close the mouth: the D holds over it; the one of 0.5 s is a rest.
        QCOMPARE(text(cues), QStringLiteral("X 0-0.1, A 0.1-0.2, D 0.2-0.45, A 0.45-0.5, X 0.5-1, G 1-1.1, X 1.1-1.5"));
        // Mouths without G, H and X: B, C and A instead, and neighbours made the same are one.
        cues = doc::mouthCuesFromPhones(phones, 1.5, "");
        QCOMPARE(text(cues), QStringLiteral("A 0-0.2, D 0.2-0.45, A 0.45-1, B 1-1.1, A 1.1-1.5"));
        // Nothing said: one rest.
        QCOMPARE(text(doc::mouthCuesFromPhones({}, 2.0)), QStringLiteral("X 0-2"));
    }

    // The filter banks are lhotse's (within float rounding), at its frame count, ends included.
    void filterBanksAreKaldis()
    {
        const auto x = referenceSignal(8000);
        int frames = 0;
        const auto f = io::kaldiFbank(x.data(), x.size(), &frames);
        QCOMPARE(frames, 50);
        QCOMPARE(f.size(), std::size_t(50 * io::kFbankBins));
        const struct
        {
            int frame, bin;
            double value;
        } reference[] = {{0, 0, -4.2353}, {0, 10, 0.2493},   {0, 40, 0.2474},   {0, 79, -0.771},
                         {1, 0, -11.0145}, {1, 10, -4.7561}, {1, 40, -3.2521},  {1, 79, -0.6998},
                         {25, 0, -10.8209}, {25, 10, -4.8607}, {25, 40, -3.2533}, {25, 79, -0.7055},
                         {49, 0, -4.3464}, {49, 10, 0.1544}, {49, 40, -1.1815}, {49, 79, -0.8198}};
        for (const auto &r : reference)
            QVERIFY2(std::abs(f[std::size_t(r.frame * io::kFbankBins + r.bin)] - r.value) < 1e-3,
                     qPrintable(QStringLiteral("frame %1 bin %2").arg(r.frame).arg(r.bin)));
        double sum = 0.0;
        for (const float v : f)
            sum += v;
        QVERIFY(std::abs(sum - -13481.16) < 1.0);
        int n = 0;
        io::kaldiFbank(x.data(), 333, &n);
        QCOMPARE(n, 2);
        const auto longer = referenceSignal(16123);
        io::kaldiFbank(longer.data(), longer.size(), &n);
        QCOMPARE(n, 101);
    }

    // Resampling keeps a tone that the new rate can hold, and drops one it cannot (rather than folding it down).
    void resamplingKeepsWhatFits()
    {
        auto tone = [](double hz, int rate, int n) {
            std::vector<float> x(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
                x[std::size_t(i)] = float(0.5 * std::sin(2 * std::numbers::pi * hz * i / rate));
            return x;
        };
        auto rms = [](const std::vector<float> &x) {
            double s = 0.0;
            for (std::size_t i = x.size() / 4; i < x.size() * 3 / 4; ++i) // away from the ends
                s += double(x[i]) * x[i];
            return std::sqrt(s / double(x.size() / 2));
        };
        const auto low = io::resampled(tone(1000, 44100, 44100), 44100, 16000);
        QCOMPARE(low.size(), std::size_t(16000));
        QVERIFY(std::abs(rms(low) - 0.5 / std::sqrt(2.0)) < 0.01);
        const auto high = io::resampled(tone(12000, 44100, 44100), 44100, 16000); // above 8 kHz
        QVERIFY(rms(high) < 0.01);
        const auto same = io::resampled(tone(1000, 16000, 100), 16000, 16000);
        QCOMPARE(same.size(), std::size_t(100));
    }

    void tokensAreRead()
    {
        const auto tokens = io::parseZipaTokens("<blk> 0\r\n<sos/eos> 1\n<unk> 2\n▁ 3\na 4\nɛ 5\n");
        QCOMPARE(tokens.size(), std::size_t(6));
        QCOMPARE(tokens[0], std::string("<blk>"));
        QCOMPARE(tokens[3], std::string("▁"));
        QCOMPARE(tokens[5], std::string("ɛ"));
        QVERIFY(io::parseZipaTokens("a b\n").empty());
    }

    // ZIPA from the audio track to the mouths, when this Mothion has ONNX Runtime and the model is there
    // (scripts/fetch-onnxruntime.sh, scripts/fetch-zipa.sh build-ci/src): a WAV at 44.1 kHz in stereo, decoded and
    // made 16 kHz mono, heard, and given as mouths over the whole sound; silence is a rest.
    void zipaHearsTheTrack()
    {
        if (!io::zipaBuiltIn())
            QSKIP("built without ONNX Runtime");
        const QString model = QStringLiteral(MOTHION_TEST_ZIPA_DIR);
        if (!QFileInfo::exists(model + QStringLiteral("/model.int8.onnx")))
            QSKIP("no ZIPA model in the build (scripts/fetch-zipa.sh)");

        const int rate = 44100;
        std::vector<float> sound(std::size_t(rate * 3), 0.0f); // 1 s of silence, 1 s of a vowel-like buzz, 1 s silence
        for (int i = rate; i < 2 * rate; ++i) {
            double s = 0.0;
            for (int h = 1; h <= 12; ++h) // a 120 Hz voice, its harmonics shaped round the formants of an "a"
                s += std::sin(2 * std::numbers::pi * 120 * h * i / rate)
                     * (std::exp(-std::pow((120.0 * h - 750) / 200, 2)) + 0.6 * std::exp(-std::pow((120.0 * h - 1200) / 250, 2)));
            sound[std::size_t(i)] = float(0.3 * s);
        }
        doc::AudioTrack track;
        track.format = "wav";
        track.data = std::make_shared<const std::vector<unsigned char>>(wavOf(sound, rate, 2));

        io::ZipaJob job;
        QSignalSpy progress(&job, &io::LipSyncJob::progress);
        QSignalSpy finished(&job, &io::LipSyncJob::finished);
        QSignalSpy failed(&job, &io::LipSyncJob::failed);
        io::LipSyncOptions options;
        QString reason;
        QVERIFY2(job.start(track, options, &reason, model), qPrintable(reason));
        QVERIFY(job.isRunning());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count() + failed.count(), 1, 60000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        QVERIFY(!job.isRunning());
        QCOMPARE(progress.last().first().toDouble(), 1.0);
        const auto cues = finished.first().first().value<std::vector<doc::MouthCue>>();
        QVERIFY(!cues.empty());
        QCOMPARE(cues.front().start, 0.0);
        QVERIFY(std::abs(cues.back().end - 3.0) < 0.01); // the whole sound
        QCOMPARE(cues.front().shape, 'X');                // it begins in silence
        for (const auto &p : job.phones()) // nothing heard in the silences that moves the mouth (a lone mark may come up)
            if (doc::mouthShapeOfPhone(p.phone))
                QVERIFY2(p.start > 0.9 && p.start < 2.1, qPrintable(QStringLiteral("%1 at %2").arg(QString::fromStdString(p.phone)).arg(p.start)));

        // Cancelled: nothing more is told.
        QVERIFY(job.start(track, options, &reason, model));
        job.cancel();
        QVERIFY(!job.isRunning());
        QTest::qWait(300);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(failed.count(), 0);

        // No model: refused.
        QVERIFY(!job.start(track, options, &reason, QStringLiteral("/nowhere")));
        QVERIFY(reason.contains(QStringLiteral("ZIPA")));
    }
};

QTEST_MAIN(TestLipSync)
#include "tst_lipsync.moc"
