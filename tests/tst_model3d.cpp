// 3D model cels: made from a glTF file, drawn, saved, and posed on the canvas.
#include "canvas/CanvasView.h"
#include "canvas/FrameRenderer.h"
#include "canvas/ModelRenderer.h"
#include "core/document/commands/CelCommands.h"
#include "core/rig3d/Ik.h"
#include "document/DocumentController.h"
#include "io/ProjectFile.h"
#include "rig3d_fixture.h"
#include "tools/ToolState.h"

#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace mothion;

namespace {

QString writeArm(const QTemporaryDir &dir, bool targets = false)
{
    const QString path = dir.filePath(QStringLiteral("arm.gltf"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    const std::vector<std::uint8_t> bytes = rig3d_fixture::armGltf(targets);
    file.write(reinterpret_cast<const char *>(bytes.data()), qint64(bytes.size()));
    return path;
}

// The paper of the tests: a dark grey (the model's default material is white).
const QColor kPaper(40, 40, 40);

bool isPaper(QRgb pixel)
{
    return (pixel & 0xffffff) == (kPaper.rgb() & 0xffffff);
}

// How many pixels of the image are not the paper.
int inked(const QImage &image)
{
    int n = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (!isPaper(image.pixel(x, y)))
                ++n;
    return n;
}

} // namespace

class TestModel3d : public QObject
{
    Q_OBJECT

private slots:
    void madeFromAFile()
    {
        QTemporaryDir dir;
        document::DocumentController controller;
        const int cels = controller.document().celCount();
        QString error;
        QVERIFY(controller.addModelCel(writeArm(dir), &error));
        QCOMPARE(controller.document().celCount(), cels + 1);
        const doc::Cel *cel = controller.document().cel(controller.currentCel());
        QVERIFY(cel && cel->properties().model3d);
        QCOMPARE(cel->properties().name, std::string("arm"));
        QCOMPARE(cel->model().model()->nodes.size(), std::size_t(5));
        QCOMPARE(cel->model().joints.size(), std::size_t(5));
        QCOMPARE(cel->deformer().rest.size(), std::size_t(rig3d::kCameraNumbers + 5 * rig3d::kNodeNumbers));
        QVERIFY(doc::hasChannel(cel->properties(), doc::Channel::Deform));
        QVERIFY(doc::hasChannel(cel->properties(), doc::Channel::Position));
        controller.undo();
        QCOMPARE(controller.document().celCount(), cels);
        // A file that is no model: refused, and why.
        const QString junk = dir.filePath(QStringLiteral("junk.glb"));
        QFile file(junk);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a model");
        file.close();
        QVERIFY(!controller.addModelCel(junk, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(controller.document().celCount(), cels);
    }

    void drawnOnTheFrame()
    {
        QTemporaryDir dir;
        document::DocumentController controller;
        controller.newDocument(320, 240, 24, 10, kPaper);
        canvas::FrameRenderer empty;
        empty.renderAll(controller.document(), 0);
        const int before = inked(empty.image());
        QVERIFY(controller.addModelCel(writeArm(dir)));
        canvas::FrameRenderer renderer;
        renderer.renderAll(controller.document(), 0);
        const int after = inked(renderer.image());
        QVERIFY2(after > before + 200, qPrintable(QString("%1 → %2").arg(before).arg(after)));
        // The arm stands in the middle, upright (the camera frames it from the front).
        QVERIFY(!isPaper(renderer.image().pixel(160, 120)));
        QVERIFY(isPaper(renderer.image().pixel(20, 120)));
        // Hidden: not drawn.
        doc::CelProperties p = controller.document().cel(controller.currentCel())->properties();
        p.visible = false;
        controller.execute(doc::makeSetCelProperties(controller.document(), controller.currentCel(), p));
        renderer.renderAll(controller.document(), 0);
        QCOMPARE(inked(renderer.image()), before);
    }

    void openGlAndCpuAgree()
    {
        QTemporaryDir dir;
        document::DocumentController controller;
        controller.newDocument(200, 200, 24, 10, Qt::white);
        QVERIFY(controller.addModelCel(writeArm(dir)));
        const doc::Cel *cel = controller.document().cel(controller.currentCel());
        const QImage first = canvas::renderModel(cel->model(), cel->deformer().rest, QSize(200, 200));
        const bool gl = canvas::modelRendererUsedOpenGL();
        qputenv("MOTHION_SOFTWARE_3D", "1");
        const QImage cpu = canvas::renderModel(cel->model(), cel->deformer().rest, QSize(200, 200));
        qunsetenv("MOTHION_SOFTWARE_3D");
        QVERIFY(!canvas::modelRendererUsedOpenGL());
        if (!gl)
            QSKIP("No OpenGL here: only the CPU's picture was made");
        // The same coverage, but for the edges.
        int differ = 0, covered = 0;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 200; ++x) {
                const bool a = qAlpha(first.pixel(x, y)) > 128, b = qAlpha(cpu.pixel(x, y)) > 128;
                covered += a;
                differ += a != b;
            }
        QVERIFY(covered > 100);
        QVERIFY2(differ < covered / 10, qPrintable(QString("%1 of %2").arg(differ).arg(covered)));
    }

    void savedAndOpened()
    {
        QTemporaryDir dir;
        document::DocumentController controller;
        controller.setSceneLength(20);
        QVERIFY(controller.addModelCel(writeArm(dir, true)));
        const doc::CelId id = controller.currentCel();
        rig3d::JointSettings s;
        s.anchor = true;
        s.stiffness = 0.25;
        s.limitTwist = true;
        s.twistMin = -12;
        s.twistMax = 30;
        s.limitBend[1] = true;
        s.bendMin[1] = -5;
        s.bendMax[1] = 60;
        s.rectangle = true;
        s.softZone = 7;
        QVERIFY(controller.setModelJoint(id, 1, s));
        const doc::Cel *cel = controller.document().cel(id);
        rig3d::Pose pose = rig3d::Pose::fromNumbers(*cel->model().model(), cel->deformer().rest);
        pose.rotations[2] = rig3d::Quat::axisAngle({0, 0, 1}, 0.5);
        pose.morphs[0] = 0.75;
        QVERIFY(controller.setDeformKey(id, 4, pose.toNumbers()));

        const QString path = dir.filePath(QStringLiteral("model.moth"));
        QString error;
        QVERIFY2(io::saveProject(controller.document(), path, &error), qPrintable(error));
        std::unique_ptr<doc::Document> loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        const doc::Cel *back = loaded->cel(id);
        QVERIFY(back && back->properties().model3d);
        QCOMPARE(back->model().source->bytes, cel->model().source->bytes);
        QCOMPARE(back->model().source->fileName, std::string("arm.gltf"));
        QVERIFY(back->model().joints[1] == s);
        QVERIFY(back->model().joints[2] == rig3d::JointSettings{});
        QCOMPARE(back->deformer().rest, cel->deformer().rest);
        QCOMPARE(loaded->deformPoseAt(*back, 4), controller.document().deformPoseAt(*cel, 4));
    }

    // The branch the model cels were made on saved them as format 21 (which main gave to the time layers).
    void openedFromTheBranchFormat()
    {
        QTemporaryDir dir;
        document::DocumentController controller;
        QVERIFY(controller.addModelCel(writeArm(dir)));
        const doc::CelId id = controller.currentCel();
        const QString path = dir.filePath(QStringLiteral("branch.moth"));
        QString error;
        QVERIFY2(io::saveProject(controller.document(), path, &error), qPrintable(error));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("downgrade"));
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec(QStringLiteral("drop table time_layer_places")));
            QVERIFY(q.exec(QStringLiteral("drop table time_layers")));
            QVERIFY(q.exec(QStringLiteral("pragma user_version = 21")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("downgrade"));
        std::unique_ptr<doc::Document> loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        QVERIFY(loaded->cel(id) && loaded->cel(id)->properties().model3d);
        QVERIFY(!loaded->cel(id)->model().isEmpty());
    }

    void copiedAndPasted()
    {
        QTemporaryDir dir;
        document::DocumentController controller;
        QVERIFY(controller.addModelCel(writeArm(dir)));
        const doc::CelId id = controller.currentCel();
        rig3d::JointSettings s;
        s.anchor = true;
        QVERIFY(controller.setModelJoint(id, 1, s));
        controller.copyCel(id);
        const doc::CelId pasted = controller.pasteCel();
        QVERIFY(pasted != doc::kNoId && pasted != id);
        const doc::Cel *copy = controller.document().cel(pasted);
        QVERIFY(copy->properties().model3d);
        QVERIFY(copy->model() == controller.document().cel(id)->model()); // the model and the joints' settings
        QCOMPARE(copy->deformer().rest, controller.document().cel(id)->deformer().rest);
    }

    void jointSettingsAreAnUndoStep()
    {
        QTemporaryDir dir;
        document::DocumentController controller;
        QVERIFY(controller.addModelCel(writeArm(dir)));
        const doc::CelId id = controller.currentCel();
        rig3d::JointSettings s;
        s.follow = true;
        QVERIFY(controller.setModelJoint(id, 2, s));
        QVERIFY(!controller.setModelJoint(id, 2, s)); // nothing changes
        QVERIFY(controller.document().cel(id)->model().joints[2].follow);
        controller.undo();
        QVERIFY(!controller.document().cel(id)->model().joints[2].follow);
        QVERIFY(!controller.setModelJoint(id, 99, s));
    }

    void posedOnTheCanvas()
    {
        QTemporaryDir dir;
        tools::ToolState state;
        document::DocumentController controller;
        controller.newDocument(800, 600, 24, 20, Qt::white);
        QVERIFY(controller.addModelCel(writeArm(dir)));
        const doc::CelId id = controller.currentCel();
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush); // whatever the tool
        canvas::ModelGizmo &gizmo = canvas.modelGizmo();
        QVERIFY(gizmo.editable());
        const QTransform toView = canvas.view().documentToView();
        auto at = [&](const QPointF &docPoint) { return toView.map(docPoint).toPoint(); };
        auto drag = [&](QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = {}) {
            QTest::mousePress(&canvas, Qt::LeftButton, modifiers, at(from));
            for (int k = 1; k <= 6; ++k)
                QTest::mouseMove(&canvas, at(from + (to - from) * k / 6.0));
            QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, at(to));
        };
        auto tipNow = [&] { return *gizmo.jointPoint(3); };
        const rig3d::Model &model = *controller.document().cel(id)->model().model();

        // Shift+drag on the tip: IK, a key on the frame, the tip where the pointer went.
        controller.setCurrentFrame(3);
        const QPointF tip = tipNow();
        const QPointF root = *gizmo.jointPoint(0);
        const QPointF target = root + (tip - root) * 0.5 + QPointF((tip - root).y() * -0.4, 0);
        drag(tip, target, Qt::ShiftModifier);
        const doc::Cel *cel = controller.document().cel(id);
        QVERIFY(cel->motion().keyAt(doc::Channel::Deform, 3));
        QVERIFY2(QLineF(tipNow(), target).length() < 3,
                 qPrintable(QString("tip at %1,%2, wanted %3,%4").arg(tipNow().x()).arg(tipNow().y()).arg(target.x()).arg(target.y())));
        controller.undo();
        QVERIFY(!controller.document().cel(id)->motion().hasKeys(doc::Channel::Deform));
        QVERIFY(QLineF(tipNow(), tip).length() < 0.5);

        // A plain drag on the tip: only its parent turns, pointing it at the pointer.
        const QPointF lower = *gizmo.jointPoint(2);
        const QPointF aside = lower + QPointF(QLineF(lower, tip).length(), 0);
        drag(tip, aside);
        rig3d::Pose pose = rig3d::Pose::fromNumbers(model, controller.document().deformPoseAt(*controller.document().cel(id), 3));
        QVERIFY(std::abs(rig3d::dot(pose.rotations[1], model.nodes[1].rotation)) > 1 - 1e-12); // the upper arm stays
        QVERIFY(std::abs(rig3d::dot(pose.rotations[2], model.nodes[2].rotation)) < 0.99);      // the forearm turned
        const QPointF now = tipNow();
        QVERIFY(std::abs(now.y() - lower.y()) < 3 && now.x() > lower.x());

        // Ctrl+drag on a joint: it twists round its bone (the joint stays where it is).
        const QPointF upper = *gizmo.jointPoint(1);
        const rig3d::Quat before = pose.rotations[1];
        drag(upper, upper + QPointF(40, 30), Qt::ControlModifier);
        pose = rig3d::Pose::fromNumbers(model, controller.document().deformPoseAt(*controller.document().cel(id), 3));
        QVERIFY(std::abs(rig3d::dot(pose.rotations[1], before)) < 1 - 1e-6);
        QVERIFY(QLineF(*gizmo.jointPoint(1), upper).length() < 0.5);

        // A drag on nothing: the camera turns round the model.
        const double yaw = pose.camera.yaw;
        drag(QPointF(60, 60), QPointF(160, 60));
        pose = rig3d::Pose::fromNumbers(model, controller.document().deformPoseAt(*controller.document().cel(id), 3));
        QVERIFY(std::abs(pose.camera.yaw - yaw) > 10);
        // ... but not with the tools that move cels: their own drag.
        state.setActiveTool(tools::ToolId::Transform);
        QVERIFY(!gizmo.press(QPointF(60, 60), canvas.view()));
    }

    void doubleClickAsksForSettings()
    {
        QTemporaryDir dir;
        tools::ToolState state;
        document::DocumentController controller;
        controller.newDocument(800, 600, 24, 20, Qt::white);
        QVERIFY(controller.addModelCel(writeArm(dir)));
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setRigMode(true);
        QSignalSpy asked(&canvas, &canvas::CanvasView::modelJointEditRequested);
        const QPoint joint = canvas.view().documentToView().map(*canvas.modelGizmo().jointPoint(2)).toPoint();
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, joint);
        QCOMPARE(canvas.modelGizmo().chosenJoint(), 2);
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, joint);
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.front().at(1).toInt(), 2);
        // In rig mode a joint is chosen, not moved: no key.
        QVERIFY(!controller.document().cel(controller.currentCel())->motion().hasKeys(doc::Channel::Deform));
    }
};

QTEST_MAIN(TestModel3d)
#include "tst_model3d.moc"
