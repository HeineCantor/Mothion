#include "document/DocumentController.h"

#include <QSignalSpy>
#include <QtTest>

using namespace mothion;

class TestCameraDocument : public QObject
{
    Q_OBJECT

private slots:
    void aCameraIsAddedWholeAndOnlyOnce()
    {
        document::DocumentController controller;
        const int w = controller.document().width(), h = controller.document().height();
        QVERIFY(!controller.camera());
        QSignalSpy changed(&controller, &document::DocumentController::cameraChanged);
        controller.addCamera();
        QVERIFY(controller.camera());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(controller.camera()->poseAt(0), doc::CameraPose::wholeSheet(w, h));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo camera"));
        controller.addCamera(); // one for now
        QCOMPARE(controller.camera()->keys().size(), size_t(1));
        QCOMPARE(changed.count(), 1);
        controller.undo();
        QVERIFY(!controller.camera());
        QCOMPARE(changed.count(), 2);
        controller.redo();
        QVERIFY(controller.camera());
        controller.removeCamera();
        QVERIFY(!controller.camera());
        controller.removeCamera(); // nothing to take away
        QCOMPARE(changed.count(), 4);
    }

    void keysAreSetMovedEasedAndRemovedWithUndo()
    {
        document::DocumentController controller;
        controller.addCamera();
        controller.setCameraKey(24, {100, 200, 2, 10});
        QCOMPARE(controller.camera()->keys().size(), size_t(2));
        QCOMPARE(controller.camera()->poseAt(24).zoom, 2.0);
        // The same pose again is no step.
        const QString undoText = controller.undoText();
        controller.setCameraKey(24, {100, 200, 2, 10});
        QCOMPARE(controller.undoText(), undoText);
        controller.undo(); // the key goes
        QCOMPARE(controller.camera()->keys().size(), size_t(1));
        controller.redo();

        QVERIFY(controller.moveCameraKey(24, 30));
        QVERIFY(controller.camera()->keyAt(30));
        QVERIFY(!controller.camera()->keyAt(24));
        QVERIFY(!controller.moveCameraKey(0, 30)); // taken
        QVERIFY(controller.setCameraEasing(30, doc::Easing::hold()));
        QCOMPARE(controller.camera()->keyAt(30)->easing, doc::Easing::hold());
        QVERIFY(!controller.setCameraEasing(31, doc::Easing::hold())); // no key there
        QVERIFY(controller.removeCameraKey(30));
        QVERIFY(!controller.removeCameraKey(30));
        QCOMPARE(controller.camera()->keys().size(), size_t(1));
        controller.undo();
        QVERIFY(controller.camera()->keyAt(30));
    }

    void aKeyMakesTheCameraIfThereIsNone()
    {
        document::DocumentController controller;
        controller.setCameraKey(5, {50, 60, 1.5, 0});
        QVERIFY(controller.camera());
        QCOMPARE(controller.camera()->keys().size(), size_t(1));
        QCOMPARE(controller.camera()->keys()[0].frame, 5);
    }

    void anInvisibleCameraIsNotApplied()
    {
        document::DocumentController controller;
        const int w = controller.document().width(), h = controller.document().height();
        QCOMPARE(controller.cameraPoseAt(10), doc::CameraPose::wholeSheet(w, h)); // no camera
        controller.addCamera();
        controller.setCameraKey(20, {100, 100, 4, 90});
        QCOMPARE(controller.cameraPoseAt(20).zoom, 4.0);
        controller.setCameraVisible(false);
        QCOMPARE(controller.cameraPoseAt(20), doc::CameraPose::wholeSheet(w, h));
        QVERIFY(controller.camera()); // still there, with its keys
        QCOMPARE(controller.camera()->keys().size(), size_t(2));
        controller.setCameraVisible(false); // already so: no step
        controller.undo();
        QCOMPARE(controller.cameraPoseAt(20).zoom, 4.0);
    }

    void aDragIsOneStepFromWhatItWasBefore()
    {
        document::DocumentController controller;
        controller.addCamera();
        const std::optional<doc::Camera> before = controller.camera();
        QSignalSpy changed(&controller, &document::DocumentController::cameraChanged);
        std::optional<doc::Camera> shown = before;
        for (int i = 1; i <= 10; ++i) { // the camera follows the pen, nothing is recorded
            shown->setKey(12, {100.0 + i, 100, 1 + i / 10.0, 0});
            controller.previewCamera(shown);
        }
        QCOMPARE(changed.count(), 10);
        QCOMPARE(controller.camera(), shown);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo camera")); // the step of adding it, not of the drag
        controller.commitCamera(before, shown);
        QCOMPARE(controller.camera(), shown);
        controller.undo(); // the whole drag at once
        QCOMPARE(controller.camera(), before);
        controller.redo();
        QCOMPARE(controller.camera(), shown);
        // The camera shown can itself be what is committed (the canvas does so): the step is still recorded.
        controller.undo();
        shown->setKey(12, {300, 300, 3, 0});
        controller.previewCamera(shown);
        controller.commitCamera(before, controller.camera());
        QCOMPARE(controller.camera(), shown);
        controller.undo();
        QCOMPARE(controller.camera(), before);
        controller.redo();
        // A drag that ends where it began records nothing.
        const bool couldUndo = controller.canUndo();
        const QString text = controller.undoText();
        controller.commitCamera(shown, shown);
        QCOMPARE(controller.canUndo(), couldUndo);
        QCOMPARE(controller.undoText(), text);
    }

    void aSelectedCameraRefusesDrawing()
    {
        document::DocumentController controller;
        QSignalSpy selection(&controller, &document::DocumentController::cameraSelectionChanged);
        controller.selectCamera(true); // there is none: nothing to select
        QVERIFY(!controller.cameraSelected());
        QCOMPARE(selection.count(), 0);
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("x"));
        controller.addCamera();
        controller.selectCamera(true);
        QVERIFY(controller.cameraSelected());
        QCOMPARE(selection.count(), 1);
        QString reason;
        QVERIFY(!controller.beginPaint(true, &reason)); // no stroke, fill, paste, shape or effect
        QVERIFY(!reason.isEmpty());
        QVERIFY(!controller.pasteImage(image, QPoint(5, 5), &reason));
        QVERIFY(controller.beginPaintLayers(&reason).empty()); // the move and the transform are refused too
        QVERIFY(!reason.isEmpty());
        // Taken away (by undo too), the camera is not selected any more.
        controller.undo(); // the camera
        QVERIFY(!controller.camera());
        QVERIFY(!controller.cameraSelected());
        QCOMPARE(selection.count(), 2);
        QVERIFY(controller.beginPaint(true, &reason)); // drawing again
        controller.cancelPaint();
        // Another project: not selected either.
        controller.redo();
        controller.selectCamera(true);
        controller.newDocument();
        QVERIFY(!controller.cameraSelected());
    }

    void aNewDocumentHasNoCamera()
    {
        document::DocumentController controller;
        controller.addCamera();
        controller.newDocument();
        QVERIFY(!controller.camera());
    }
};

QTEST_MAIN(TestCameraDocument)
#include "tst_cameradoc.moc"
