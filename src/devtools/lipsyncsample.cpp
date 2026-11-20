// Developer tool: writes a project to try the lip sync on.
//
//   mothion_lipsyncsample out.moth
//
// A face ("Head") and a mouth chart over it ("Mouth", a mouth chart cel: a drawing for each of Preston Blair's mouth
// shapes, named A to H and X, its letter written small by it), the mouth at rest (X) on the whole scene; an audio track of a voice made up
// here, saying "mam-ma, o fa-me, lu-po vi-a" (vowels from their formants, the consonants as noise, a closure, a hum);
// a marker at each syllable with the shapes it calls for, to check the work against.

#include "core/document/Document.h"
#include "core/document/LipSync.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "io/ProjectFile.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <random>

using namespace mothion;

namespace {

constexpr int kWidth = 1920, kHeight = 1080, kFps = 24;
constexpr int kRate = 22050; // samples per second
const QPointF kMouth(960, 700);

// --- the voice -------------------------------------------------------------------------------------------------------

// A sound of the made-up voice: a vowel or voiced consonant (formants), a noise (f, s), a closure (p), or silence.
struct Sound
{
    enum class Kind { Voiced, Noise, Closure, Silence };
    Kind kind = Kind::Silence;
    double seconds = 0.1;
    std::array<double, 3> formants{}; // Hz
    double loudness = 1.0;
    bool voicedToo = false; // v: noise and voice
};

Sound vowel(char v, double seconds)
{
    Sound s{Sound::Kind::Voiced, seconds, {}, 1.0, false};
    switch (v) {
    case 'a': s.formants = {800, 1200, 2500}; break;
    case 'e': s.formants = {480, 1800, 2500}; break;
    case 'i': s.formants = {300, 2300, 3000}; break;
    case 'o': s.formants = {500, 900, 2500}; break;
    default: s.formants = {320, 800, 2400}; break; // u
    }
    return s;
}
Sound hum(double seconds) { return {Sound::Kind::Voiced, seconds, {250, 1000, 2200}, 0.35, false}; }   // m
Sound ell(double seconds) { return {Sound::Kind::Voiced, seconds, {360, 1300, 2700}, 0.55, false}; }   // l
Sound noise(double seconds, bool voiced) { return {Sound::Kind::Noise, seconds, {}, 0.25, voiced}; }  // f, v
Sound closure(double seconds) { return {Sound::Kind::Closure, seconds, {}, 0.0, false}; }              // p
Sound silence(double seconds) { return {Sound::Kind::Silence, seconds, {}, 0.0, false}; }

// A two-pole resonator (one formant).
struct Resonator
{
    double a = 0, b = 0, c = 0, y1 = 0, y2 = 0;
    void tune(double frequency, double bandwidth)
    {
        const double r = std::exp(-std::numbers::pi * bandwidth / kRate);
        c = -r * r;
        b = 2.0 * r * std::cos(2.0 * std::numbers::pi * frequency / kRate);
        a = 1.0 - b - c;
    }
    double operator()(double x)
    {
        const double y = a * x + b * y1 + c * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

std::vector<float> speak(const std::vector<Sound> &sounds)
{
    std::vector<float> out;
    std::mt19937 random(7);
    std::uniform_real_distribution<double> white(-1.0, 1.0);
    std::array<Resonator, 3> formants;
    double phase = 0, total = 0;
    for (const Sound &s : sounds)
        total += s.seconds;
    double t = 0;
    std::array<double, 3> current{500, 1500, 2500};
    for (const Sound &s : sounds) {
        const int n = int(s.seconds * kRate);
        const double fade = 0.012 * kRate; // in and out of each sound, so nothing clicks
        for (int i = 0; i < n; ++i, t += 1.0 / kRate) {
            const double edge = std::min({1.0, i / fade, (n - i) / fade});
            double x = 0;
            if (s.kind == Sound::Kind::Voiced || (s.kind == Sound::Kind::Noise && s.voicedToo)) {
                // The voice's pitch falls along the phrase; a pulse each period (a sawtooth, softened).
                const double pitch = 150.0 - 35.0 * t / total + 3.0 * std::sin(2 * std::numbers::pi * 5 * t);
                phase += pitch / kRate;
                if (phase >= 1.0)
                    phase -= 1.0;
                x += (1.0 - 2.0 * phase) * 0.6;
            }
            if (s.kind == Sound::Kind::Voiced) {
                for (int k = 0; k < 3; ++k) // the formants glide to the sound's
                    current[std::size_t(k)] += (s.formants[std::size_t(k)] - current[std::size_t(k)]) * 0.004;
                for (int k = 0; k < 3; ++k)
                    formants[std::size_t(k)].tune(current[std::size_t(k)], 60.0 + 40.0 * k);
                const double v = formants[0](x) + 0.5 * formants[1](x) + 0.25 * formants[2](x);
                out.push_back(float(0.35 * s.loudness * edge * v));
            } else if (s.kind == Sound::Kind::Noise) {
                const double hiss = white(random) * 0.5 + (s.voicedToo ? formants[0](x) * 0.3 : 0.0);
                out.push_back(float(s.loudness * edge * hiss));
            } else if (s.kind == Sound::Kind::Closure) {
                // Silence, then the burst of the lips opening.
                const bool burst = i > n - int(0.015 * kRate);
                out.push_back(burst ? float(0.4 * white(random)) : 0.0f);
            } else {
                out.push_back(0.0f);
            }
        }
    }
    float peak = 0;
    for (float v : out)
        peak = std::max(peak, std::abs(v));
    if (peak > 0)
        for (float &v : out)
            v *= 0.8f / peak;
    return out;
}

std::vector<unsigned char> wav(const std::vector<float> &samples)
{
    std::vector<unsigned char> out;
    auto put = [&](std::uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
    };
    const std::uint32_t data = std::uint32_t(samples.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put(36 + data, 4);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put(16, 4);
    put(1, 2); // PCM
    put(1, 2); // mono
    put(kRate, 4);
    put(kRate * 2, 4);
    put(2, 2);
    put(16, 2);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put(data, 4);
    for (float v : samples)
        put(std::uint32_t(std::uint16_t(std::int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767)))), 2);
    return out;
}

// --- the pictures ----------------------------------------------------------------------------------------------------

void writeImage(doc::TiledImage &target, const QImage &image)
{
    const QImage premultiplied = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    target.write(0, 0, premultiplied.width(), premultiplied.height(),
                 reinterpret_cast<const doc::Pixel *>(premultiplied.constBits()), premultiplied.bytesPerLine() / 4);
}

QImage page()
{
    QImage image(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

QImage face()
{
    QImage image = page();
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(60, 40, 30), 8));
    p.setBrush(QColor(250, 214, 180));
    p.drawEllipse(QPointF(960, 560), 300, 340);
    p.setBrush(Qt::white);
    for (const double x : {850.0, 1070.0}) {
        p.setPen(QPen(QColor(60, 40, 30), 6));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(x, 470), 50, 38);
        p.setBrush(QColor(60, 40, 30));
        p.drawEllipse(QPointF(x + 6, 474), 16, 16);
    }
    p.setPen(QPen(QColor(60, 40, 30), 6, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(935, 540, 50, 60), 200 * 16, 140 * 16); // the nose
    return image;
}

// One mouth shape, at the face's mouth, its letter small by it.
QImage mouth(char shape)
{
    QImage image = page();
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor line(60, 30, 30), inside(110, 30, 40), teeth(Qt::white), tongue(220, 100, 110);
    const QPen pen(line, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    const double x = kMouth.x(), y = kMouth.y();
    auto open = [&](double w, double h) { // an open mouth, a wide oval
        p.setBrush(inside);
        p.drawEllipse(QPointF(x, y), w, h);
    };
    switch (shape) {
    case 'A': { // lips pressed together
        QPainterPath lips;
        lips.moveTo(x - 90, y);
        lips.quadTo(x, y + 10, x + 90, y);
        p.setBrush(Qt::NoBrush);
        p.drawPath(lips);
        p.drawLine(QPointF(x - 98, y - 8), QPointF(x - 86, y + 8));
        p.drawLine(QPointF(x + 98, y - 8), QPointF(x + 86, y + 8));
        break;
    }
    case 'B': // teeth together, lips a little apart
        p.setBrush(teeth);
        p.drawRoundedRect(QRectF(x - 95, y - 22, 190, 44), 22, 22);
        p.drawLine(QPointF(x - 80, y), QPointF(x + 80, y));
        break;
    case 'C': // E: open
        open(95, 45);
        p.setBrush(teeth);
        p.drawChord(QRectF(x - 80, y - 45, 160, 50), 20 * 16, 140 * 16);
        break;
    case 'D': // A: wide open, the tongue low
        open(105, 85);
        p.setBrush(tongue);
        p.setPen(Qt::NoPen);
        p.drawChord(QRectF(x - 70, y + 20, 140, 90), 0, 180 * 16);
        p.setPen(pen);
        p.setBrush(teeth);
        p.drawChord(QRectF(x - 85, y - 85, 170, 50), 30 * 16, 120 * 16);
        break;
    case 'E': // O: rounded
        open(60, 70);
        break;
    case 'F': // U, W: puckered
        open(32, 36);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(x, y), 46, 50);
        break;
    case 'G': // F, V: the upper teeth on the lower lip
        p.setBrush(inside);
        p.drawRoundedRect(QRectF(x - 85, y - 25, 170, 45), 20, 20);
        p.setBrush(teeth);
        p.drawRect(QRectF(x - 60, y - 25, 120, 22));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(x - 90, y - 10, 180, 50), 200 * 16, 140 * 16);
        break;
    case 'H': // L: open, the tongue up behind the teeth
        open(90, 60);
        p.setBrush(tongue);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(x, y - 18), 36, 30);
        p.setPen(pen);
        break;
    default: { // X: at rest
        QPainterPath lips;
        lips.moveTo(x - 70, y - 4);
        lips.quadTo(x, y + 18, x + 70, y - 4);
        p.setBrush(Qt::NoBrush);
        p.drawPath(lips);
        break;
    }
    }
    p.setPen(QColor(120, 120, 120));
    QFont font = p.font();
    font.setPixelSize(40);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QPointF(x + 160, y + 100), QString(QChar(shape)));
    return image;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out.moth\n", argv[0]);
        return 2;
    }

    // "mam-ma, o fa-me, lu-po vi-a": the sounds, and the syllables with the shapes each calls for.
    struct Syllable
    {
        const char *text;
        std::vector<Sound> sounds;
    };
    const std::vector<Syllable> said = {
        {nullptr, {silence(0.40)}},
        {"mam: A D A", {hum(0.09), vowel('a', 0.20), hum(0.10)}},
        {"ma: A D", {hum(0.06), vowel('a', 0.24)}},
        {nullptr, {silence(0.22)}},
        {"o: E", {vowel('o', 0.28)}},
        {nullptr, {silence(0.12)}},
        {"fa: G D", {noise(0.11, false), vowel('a', 0.20)}},
        {"me: A C", {hum(0.08), vowel('e', 0.26)}},
        {nullptr, {silence(0.38)}},
        {"lu: H F", {ell(0.08), vowel('u', 0.20)}},
        {"po: A E", {closure(0.08), vowel('o', 0.26)}},
        {nullptr, {silence(0.16)}},
        {"vi: G B", {noise(0.09, true), vowel('i', 0.18)}},
        {"a: D", {vowel('a', 0.30)}},
        {nullptr, {silence(0.50)}},
    };
    std::vector<Sound> sounds;
    std::map<int, std::string> markers;
    double seconds = 0;
    for (const Syllable &s : said) {
        if (s.text)
            markers[int(std::floor(seconds * kFps))] = s.text;
        for (const Sound &sound : s.sounds) {
            sounds.push_back(sound);
            seconds += sound.seconds;
        }
    }
    const int frames = int(std::ceil(seconds * kFps));

    doc::Document d(kWidth, kHeight, doc::FrameRate{kFps, 1});
    d.setFrameCount(frames);
    d.setPaperColor(0xffffffffu);

    // The face, held the whole scene.
    doc::AddCelCommand head(d, 0, {"Head"});
    head.apply(d);
    const doc::CelId headId = d.celAt(0).id();
    doc::NewDrawing faceDrawing = doc::makeNewDrawing(d, headId, 0, frames, "Face");
    faceDrawing.command->apply(d);
    writeImage(d.layer(headId, faceDrawing.id, faceDrawing.layer)->image, face());

    // The mouth chart over it: a drawing per shape, named after it; at rest (X) everywhere to begin with.
    doc::CelProperties mouthProperties{"Mouth"};
    mouthProperties.mouthChart = true; // the Timeline's mouth bar and the Mouth chart panel are for it
    doc::AddCelCommand mouthCel(d, 1, mouthProperties);
    mouthCel.apply(d);
    const doc::CelId mouthId = d.celAt(1).id();
    doc::DrawingId rest = doc::kNoId;
    for (const char shape : doc::kMouthShapes) {
        doc::NewDrawing drawing = doc::makeNewDrawing(d, mouthId, 0, 1, "Mouth");
        drawing.command->apply(d);
        d.cel(mouthId)->drawing(drawing.id)->name = std::string(1, shape);
        writeImage(d.layer(mouthId, drawing.id, drawing.layer)->image, mouth(shape));
        if (shape == 'X')
            rest = drawing.id;
    }
    d.cel(mouthId)->timing().setExposure(0, frames, rest);

    doc::AudioTrack track;
    track.name = "mamma ho fame";
    track.format = "wav";
    track.data = std::make_shared<const std::vector<unsigned char>>(wav(speak(sounds)));
    track.duration = seconds;
    d.setAudio(track);
    d.setMarkers(markers);

    QString error;
    if (!io::saveProject(d, QString::fromLocal8Bit(argv[1]), &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    std::printf("%s: %d frames at %d fps, %.2f s of audio, %zu syllables marked\n", argv[1], frames, kFps, seconds,
                markers.size());
    return 0;
}
