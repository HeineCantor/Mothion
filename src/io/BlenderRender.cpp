#include "io/BlenderRender.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace mothion::io {

QString findBlender(const QString &preferred)
{
    if (!preferred.isEmpty() && QFileInfo(preferred).isExecutable())
        return preferred;
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("blender.exe");
#else
    const QString name = QStringLiteral("blender");
#endif
    if (const QString found = QStandardPaths::findExecutable(name); !found.isEmpty())
        return found;
    // Where the installers put it, when it is not on the search path.
    QStringList places;
#ifdef Q_OS_WIN
    for (const QString &base : {QStringLiteral("C:/Program Files/Blender Foundation"),
                                QStringLiteral("C:/Program Files (x86)/Blender Foundation")}) {
        const QStringList versions = QDir(base).entryList({QStringLiteral("Blender*")}, QDir::Dirs, QDir::Name | QDir::Reversed);
        for (const QString &version : versions)
            places << base + QLatin1Char('/') + version + QStringLiteral("/blender.exe");
    }
#elif defined(Q_OS_MACOS)
    places << QStringLiteral("/Applications/Blender.app/Contents/MacOS/Blender");
#else
    places << QStringLiteral("/usr/bin/blender") << QStringLiteral("/usr/local/bin/blender")
           << QStringLiteral("/snap/bin/blender") << QDir::homePath() + QStringLiteral("/.local/bin/blender");
#endif
    for (const QString &place : places)
        if (QFileInfo(place).isExecutable())
            return place;
    return {};
}

BlenderLine parseBlenderLine(const QString &line)
{
    static const QRegularExpression saved(QStringLiteral("Saved: '(.*)'\\s*$"));
    static const QRegularExpression range(QStringLiteral("^MOTHION_RANGE (-?\\d+) (-?\\d+)\\s*$"));
    BlenderLine out;
    if (const auto m = range.match(line); m.hasMatch()) {
        out.kind = BlenderLine::Kind::Range;
        out.first = m.captured(1).toInt();
        out.last = m.captured(2).toInt();
    } else if (const auto m2 = saved.match(line); m2.hasMatch()) {
        out.kind = BlenderLine::Kind::Saved;
        out.text = m2.captured(1);
    } else if (line.startsWith(QLatin1String("MOTHION_ERROR "))) {
        out.kind = BlenderLine::Kind::Error;
        out.text = line.mid(14).trimmed();
    } else if (line.startsWith(QLatin1String("MOTHION_DONE"))) {
        out.kind = BlenderLine::Kind::Done;
    }
    return out;
}

QString blenderScript()
{
    return QStringLiteral(R"PY(import bpy, sys, json, os

args = json.loads(sys.argv[sys.argv.index("--") + 1])
scene = bpy.context.scene


def fail(message):
    print("MOTHION_ERROR " + message, flush=True)
    sys.exit(3)


if scene.camera is None:
    fail("The scene has no active camera.")
render = scene.render
render.resolution_x = args["width"]
render.resolution_y = args["height"]
render.resolution_percentage = args["percent"]
render.film_transparent = True
render.image_settings.file_format = "PNG"
render.image_settings.color_mode = "RGBA"
render.image_settings.color_depth = "8"
render.image_settings.compression = 0
render.use_file_extension = True
render.use_overwrite = True
# A draft: Eevee (whatever the version of Blender calls it) with one sample, the compositing as the scene has it (it is the
# look of the picture), no sequencer, and the scene kept in memory from a frame to the next (the first frame is the slow one).
engines = [e.identifier for e in render.bl_rna.properties["engine"].enum_items]
eevee = next((e for e in engines if e.startswith("BLENDER_EEVEE")), None)
if eevee:
    render.engine = eevee
    scene.eevee.taa_render_samples = 1
render.use_sequencer = False
render.use_persistent_data = True
render.use_motion_blur = False
if not args["lineart"]:
    # Line Art makes most of the time of a frame: off, in every object that has it.
    for obj in scene.objects:
        for modifier in obj.modifiers:
            if modifier.type in ("LINEART", "GREASE_PENCIL_LINEART"):
                modifier.show_render = False
if args["first"] >= 0:
    scene.frame_start = args["first"]
if args["last"] >= 0:
    scene.frame_end = max(args["last"], scene.frame_start)
scene.frame_step = 1
render.filepath = os.path.join(args["out"], "frame_")
first, last = scene.frame_start, scene.frame_end
print("MOTHION_RANGE %d %d" % (first, last), flush=True)
# From `startOffset` frames in to the end, then the ones before: what is looked at is made first.
pivot = min(max(first + args["offset"], first), last)
segments = [(pivot, last)] + ([(first, pivot - 1)] if pivot > first else [])
for a, b in segments:
    scene.frame_start, scene.frame_end = a, b
    bpy.ops.render.render(animation=True)
print("MOTHION_DONE", flush=True)
)PY");
}

BlenderRender::BlenderRender(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, &BlenderRender::readOutput);
    connect(&m_process, &QProcess::finished, this, &BlenderRender::processFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit finished(false, tr("Blender could not be started (%1).").arg(m_process.program()), {});
        }
    });
}

BlenderRender::~BlenderRender()
{
    if (isRunning()) {
        m_process.disconnect(this);
        m_process.kill();
        m_process.waitForFinished(2000);
    }
}

void BlenderRender::start(const QString &blendFile, const QString &outputDir, const BlenderOptions &options)
{
    if (isRunning())
        return;
    m_outputDir = outputDir;
    m_buffer.clear();
    m_error.clear();
    m_tail.clear();
    m_first = m_last = m_done = 0;
    m_cancelled = false;
    QDir().mkpath(outputDir);
    const QString scriptPath = QDir(outputDir).filePath(QStringLiteral("render.py"));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit finished(false, tr("Cannot write in %1.").arg(outputDir), {});
        return;
    }
    script.write(blenderScript().toUtf8());
    script.close();
    const QJsonObject json{{"out", QDir::toNativeSeparators(outputDir)}, {"width", options.width},
                           {"height", options.height}, {"percent", std::clamp(options.percent, 1, 100)},
                           {"lineart", options.lineArt}, {"first", options.firstFrame}, {"last", options.lastFrame},
                           {"offset", std::max(0, options.startOffset)}};
    m_process.start(options.blender,
                    {QStringLiteral("-b"), blendFile, QStringLiteral("--python"), scriptPath, QStringLiteral("--"),
                     QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact))});
}

void BlenderRender::cancel()
{
    if (!isRunning())
        return;
    m_cancelled = true;
    m_process.kill();
}

void BlenderRender::readOutput()
{
    m_buffer += QString::fromUtf8(m_process.readAll());
    int end;
    while ((end = m_buffer.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = m_buffer.left(end).trimmed();
        m_buffer.remove(0, end + 1);
        if (line.isEmpty())
            continue;
        m_tail << line;
        if (m_tail.size() > 8)
            m_tail.removeFirst();
        const BlenderLine parsed = parseBlenderLine(line);
        switch (parsed.kind) {
        case BlenderLine::Kind::Range:
            m_first = parsed.first;
            m_last = parsed.last;
            emit range(m_first, m_last);
            emit progressed(0, m_last - m_first + 1);
            break;
        case BlenderLine::Kind::Saved: {
            static const QRegularExpression number(QStringLiteral("frame_(\\d+)\\.png$"));
            if (const auto m = number.match(parsed.text); m.hasMatch())
                emit frameSaved(m.captured(1).toInt(), parsed.text);
            emit progressed(++m_done, std::max(m_last - m_first + 1, m_done));
            break;
        }
        case BlenderLine::Kind::Error: m_error = parsed.text; break;
        default: break;
        }
    }
}

void BlenderRender::processFinished(int code, QProcess::ExitStatus status)
{
    readOutput();
    if (m_cancelled) {
        emit finished(false, {}, {});
        return;
    }
    QStringList files;
    for (const QFileInfo &info : QDir(m_outputDir).entryInfoList({QStringLiteral("frame_*.png")}, QDir::Files, QDir::Name))
        files << info.absoluteFilePath();
    if (status == QProcess::NormalExit && code == 0 && m_done > 0) { // (the pictures may be taken away as they come)
        emit finished(true, {}, files);
        return;
    }
    QString why = m_error;
    if (why.isEmpty() && status == QProcess::NormalExit && code == 0)
        why = tr("The render made no pictures.");
    if (why.isEmpty())
        why = tr("Blender stopped (code %1). %2").arg(code).arg(m_tail.join(QLatin1Char('\n')));
    emit finished(false, why, {});
}

} // namespace mothion::io
