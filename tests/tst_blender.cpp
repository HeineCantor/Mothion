#include "document/DocumentController.h"
#include "io/BlenderRender.h"
#include "ui/MainWindow.h"

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mothion;

namespace {

// A scene saved by the Blender found here: a cube that goes along three frames, seen by a camera (or by none), drawn by the
// Workbench (the quickest). Empty if it could not be made.
QString makeScene(const QString &blender, const QString &path, bool withCamera, bool withLineArt = false)
{
    const QString python = QStringLiteral(R"PY(
import bpy
bpy.ops.wm.read_factory_settings(use_empty=True)
s = bpy.context.scene
s.render.engine = 'BLENDER_WORKBENCH'
bpy.ops.mesh.primitive_cube_add()
cube = bpy.context.object
if %1:
    data = bpy.data.cameras.new('cam')
    cam = bpy.data.objects.new('cam', data)
    s.collection.objects.link(cam)
    cam.location = (0, -6, 0)
    cam.rotation_euler = (1.5708, 0, 0)
    s.camera = cam
if %3:
    bpy.ops.object.grease_pencil_add(type='LINEART_SCENE')
s.frame_start = 1
s.frame_end = 3
cube.location.x = -1
cube.keyframe_insert('location', frame=1)
cube.location.x = 1
cube.keyframe_insert('location', frame=3)
bpy.ops.wm.save_as_mainfile(filepath=r'%2')
)PY").arg(withCamera ? QStringLiteral("True") : QStringLiteral("False"), path,
          withLineArt ? QStringLiteral("True") : QStringLiteral("False"));
    QProcess process;
    process.start(blender, {QStringLiteral("-b"), QStringLiteral("--factory-startup"), QStringLiteral("--python-expr"), python});
    if (!process.waitForFinished(120000) || process.exitCode() != 0)
        return {};
    return QFile::exists(path) ? path : QString();
}

} // namespace

class TestBlender : public QObject
{
    Q_OBJECT

private slots:
    void theRenderTellsWhatItDoesInLines()
    {
        using Kind = io::BlenderLine::Kind;
        QCOMPARE(io::parseBlenderLine(QStringLiteral("MOTHION_RANGE 1 24")).kind, Kind::Range);
        QCOMPARE(io::parseBlenderLine(QStringLiteral("MOTHION_RANGE 1 24")).last, 24);
        const io::BlenderLine saved = io::parseBlenderLine(QStringLiteral("Saved: '/tmp/x/frame_0003.png'"));
        QCOMPARE(saved.kind, Kind::Saved);
        QCOMPARE(saved.text, QStringLiteral("/tmp/x/frame_0003.png"));
        // Blender puts the time before its lines.
        QCOMPARE(io::parseBlenderLine(QStringLiteral("00:01.198  render           | Saved: '/tmp/x/frame_0001.png'")).text,
                 QStringLiteral("/tmp/x/frame_0001.png"));
        const io::BlenderLine error = io::parseBlenderLine(QStringLiteral("MOTHION_ERROR The scene has no active camera."));
        QCOMPARE(error.kind, Kind::Error);
        QCOMPARE(error.text, QStringLiteral("The scene has no active camera."));
        QCOMPARE(io::parseBlenderLine(QStringLiteral("Fra:3 Mem:12M | Time:00:01")).kind, Kind::Nothing);
        QVERIFY(io::blenderScript().contains(QLatin1String("film_transparent")));
        QVERIFY(io::findBlender().isEmpty() || QFileInfo(io::findBlender()).isExecutable());
    }

    // The scene, rendered by its active camera at the size asked, one picture for each frame, with the background
    // transparent. (Needs Blender: skipped where there is none.)
    void aSceneIsRenderedFrameByFrame()
    {
        const QString blender = io::findBlender();
        if (blender.isEmpty())
            QSKIP("Blender is not installed here");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scene = makeScene(blender, dir.filePath(QStringLiteral("scene.blend")), true);
        QVERIFY2(!scene.isEmpty(), "the test scene could not be made");

        io::BlenderOptions options;
        options.blender = blender;
        options.width = 64;
        options.height = 48;
        options.percent = 100;
        io::BlenderRender render;
        QSignalSpy finished(&render, &io::BlenderRender::finished);
        QSignalSpy progressed(&render, &io::BlenderRender::progressed);
        render.start(scene, dir.filePath(QStringLiteral("out")), options);
        QVERIFY(finished.wait(120000));
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(finished.first().at(1).toString()));
        const QStringList files = finished.first().at(2).toStringList();
        QCOMPARE(files.size(), 3);
        QVERIFY(files.first().endsWith(QLatin1String("frame_0001.png")));
        QVERIFY(progressed.size() >= 3);
        QCOMPARE(progressed.last().at(0).toInt(), 3);
        const QImage first(files.first()), last(files.last());
        QCOMPARE(first.size(), QSize(64, 48));
        QVERIFY(first.hasAlphaChannel());
        QCOMPARE(qAlpha(first.pixel(0, 0)), 0);            // the background is left out
        QVERIFY(qAlpha(first.pixel(32 - 12, 24)) > 200);   // the cube, on the left at the first frame
        QCOMPARE(qAlpha(first.pixel(32 + 12, 24)), 0);
        QVERIFY(qAlpha(last.pixel(32 + 12, 24)) > 200);    // and on the right at the last
    }

    // A draft is a part of the size, and leaves the Line Art out unless asked (it is most of what a frame takes).
    void aDraftIsSmallAndLeavesTheLineArtOut()
    {
        const QString blender = io::findBlender();
        if (blender.isEmpty())
            QSKIP("Blender is not installed here");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scene = makeScene(blender, dir.filePath(QStringLiteral("lines.blend")), true, true);
        QVERIFY2(!scene.isEmpty(), "the test scene could not be made");
        auto firstPicture = [&](bool lineArt, const QString &out) {
            io::BlenderOptions options;
            options.blender = blender;
            options.width = 256;
            options.height = 192;
            options.percent = 50;
            options.lineArt = lineArt;
            options.firstFrame = 1;
            options.lastFrame = 1;
            io::BlenderRender render;
            QSignalSpy finished(&render, &io::BlenderRender::finished);
            render.start(scene, dir.filePath(out), options);
            if (!finished.wait(120000) || !finished.first().at(0).toBool())
                return QImage();
            return QImage(finished.first().at(2).toStringList().first());
        };
        const QImage without = firstPicture(false, QStringLiteral("a"));
        const QImage with = firstPicture(true, QStringLiteral("b"));
        QVERIFY(!without.isNull() && !with.isNull());
        QCOMPARE(without.size(), QSize(128, 96)); // half of what was asked
        QCOMPARE(with.size(), QSize(128, 96));
        QVERIFY2(with != without, "the Line Art should draw something when it is kept");
    }

    // The frames are made from where the timeline is, to the end, then the ones before.
    void theFramesAreMadeFromWhereYouLook()
    {
        const QString blender = io::findBlender();
        if (blender.isEmpty())
            QSKIP("Blender is not installed here");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scene = makeScene(blender, dir.filePath(QStringLiteral("scene.blend")), true);
        QVERIFY2(!scene.isEmpty(), "the test scene could not be made");
        io::BlenderOptions options;
        options.blender = blender;
        options.width = 64;
        options.height = 48;
        options.percent = 100;
        options.startOffset = 1; // the second frame of the range
        io::BlenderRender render;
        QSignalSpy finished(&render, &io::BlenderRender::finished);
        QSignalSpy saved(&render, &io::BlenderRender::frameSaved);
        QSignalSpy range(&render, &io::BlenderRender::range);
        render.start(scene, dir.filePath(QStringLiteral("out")), options);
        QVERIFY(finished.wait(120000));
        QVERIFY(finished.first().at(0).toBool());
        QCOMPARE(range.size(), 1);
        QCOMPARE(range.first().at(0).toInt(), 1);
        QCOMPARE(range.first().at(1).toInt(), 3);
        QList<int> order;
        for (const QList<QVariant> &args : saved)
            order << args.at(0).toInt();
        QCOMPARE(order, (QList<int>{2, 3, 1}));
    }

    // A scene with no camera says so, and nothing is rendered.
    void aSceneWithoutACameraSaysSo()
    {
        const QString blender = io::findBlender();
        if (blender.isEmpty())
            QSKIP("Blender is not installed here");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scene = makeScene(blender, dir.filePath(QStringLiteral("nocam.blend")), false);
        QVERIFY2(!scene.isEmpty(), "the test scene could not be made");
        io::BlenderOptions options;
        options.blender = blender;
        io::BlenderRender render;
        QSignalSpy finished(&render, &io::BlenderRender::finished);
        render.start(scene, dir.filePath(QStringLiteral("out")), options);
        QVERIFY(finished.wait(120000));
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY2(finished.first().at(1).toString().contains(QLatin1String("camera")), qPrintable(finished.first().at(1).toString()));
    }

    // Through the window: the scene becomes a new cel with a frame for each frame of the scene, from where it was asked.
    void theWindowImportsTheSceneAsACel()
    {
        const QString blender = io::findBlender();
        if (blender.isEmpty())
            QSKIP("Blender is not installed here");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scene = makeScene(blender, dir.filePath(QStringLiteral("scene.blend")), true);
        QVERIFY2(!scene.isEmpty(), "the test scene could not be made");
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        QVERIFY(controller);
        const int cels = controller->document().celCount();
        QVERIFY(window.startBlenderImport(scene, blender, 4, 50));
        QVERIFY(!window.startBlenderImport(scene, blender, 4, 50)); // one at a time
        // The cel is there as soon as Blender has read the scene, with a frame for each frame of it.
        QTRY_COMPARE_WITH_TIMEOUT(controller->document().celCount(), cels + 1, 120000);
        const doc::Cel &cel = controller->document().celAt(controller->document().celCount() - 1);
        QCOMPARE(cel.properties().name, std::string("scene"));
        QVERIFY(cel.timing().drawingAt(4) != doc::kNoId);
        QVERIFY(cel.timing().drawingAt(6) != doc::kNoId);
        QCOMPARE(cel.timing().drawingAt(3), doc::kNoId);
        QCOMPARE(controller->currentCel(), cel.id());
        QTRY_VERIFY_WITH_TIMEOUT(!window.blenderImportRunning(), 120000);
        QCOMPARE(controller->document().celCount(), cels + 1);
        // The draft was rendered small and made as big as the page: the cube is at its middle, the corner is clear.
        const doc::Drawing *drawing = cel.drawingAt(4);
        QVERIFY(drawing);
        const doc::TiledImage &pixels = drawing->layers.front().image;
        QCOMPARE(qAlpha(pixels.pixel(0, 0)), 0);
        QVERIFY(qAlpha(pixels.pixel(controller->document().width() / 2 - int(controller->document().width() * 0.2), controller->document().height() / 2)) > 200);
        // The last frame is filled too, with the cube on the other side.
        const doc::TiledImage &lastPixels = cel.drawingAt(6)->layers.front().image;
        QVERIFY(qAlpha(lastPixels.pixel(controller->document().width() / 2 + int(controller->document().width() * 0.2), controller->document().height() / 2)) > 200);
        // Undo: the pictures, then the cel.
        controller->undo();
        QCOMPARE(controller->document().celCount(), cels + 1);
        QCOMPARE(qAlpha(controller->document().celAt(cels).drawingAt(4)->layers.front().image.pixel(controller->document().width() / 2 - int(controller->document().width() * 0.2), controller->document().height() / 2)), 0);
        controller->undo();
        QCOMPARE(controller->document().celCount(), cels);
    }
};

QTEST_MAIN(TestBlender)
#include "tst_blender.moc"
