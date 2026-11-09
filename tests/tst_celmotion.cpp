#include "canvas/CanvasView.h"
#include "canvas/FrameRenderer.h"
#include "core/document/CelMotion.h"
#include "core/document/Document.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"
#include "ui/panels/ToolPropertiesPanel.h"
#include "ui/panels/timeline/TimelinePanel.h"
#include "ui/panels/timeline/TimelineView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include <cmath>

using namespace mothion;

namespace {

doc::CelPose at(double x, double y, double rotation = 0, double scale = 1, double opacity = 1)
{
    doc::CelPose p;
    p.x = x;
    p.y = y;
    p.rotation = rotation;
    p.scaleX = p.scaleY = scale;
    p.opacity = opacity;
    return p;
}

// A cel with one drawing held for the whole scene, holding a red square of `size` at (x, y).
doc::CelId addSquare(doc::Document &document, int x, int y, int size, int index = 0, uint32_t color = 0xffff0000u)
{
    doc::AddCelCommand cel(document, index, {"piece"});
    cel.apply(document);
    doc::NewDrawing drawing = doc::makeNewDrawing(document, cel.celId(), 0, document.frameCount());
    drawing.command->apply(document);
    auto &image = document.cel(cel.celId())->drawing(drawing.id)->layers.front().image;
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i)
            image.setPixel(x + i, y + j, color);
    return cel.celId();
}

} // namespace

class TestCelMotion : public QObject
{
    Q_OBJECT

private slots:
    void aPoseBetweenKeysGoesAlongTheEasing()
    {
        doc::CelMotion motion;
        QVERIFY(motion.isEmpty());
        QCOMPARE(motion.poseAt(7), doc::CelPose{}); // no key: as drawn
        motion.setKey(10, at(100, 0, 90, 1, 1), doc::Easing::hold()); // (its easing is the one from 10 to 20)
        motion.setKey(20, at(200, 50, 270, 4, 0), doc::Easing::linear());
        motion.setKey(0, at(0, 0, 0, 1, 1), doc::Easing::linear());
        QCOMPARE(motion.keys().size(), size_t(3));
        QCOMPARE(motion.keys().front().frame, 0); // by frame, whatever the order they came in
        // Before the first and after the last: as that key put it.
        QCOMPARE(motion.poseAt(-5).x, 0.0);
        QCOMPARE(motion.poseAt(99), at(200, 50, 270, 4, 0));
        // Straight along the easing: halfway between frames 0 and 10.
        const doc::CelPose half = motion.poseAt(5);
        QCOMPARE(half.x, 50.0);
        QCOMPARE(half.rotation, 45.0);
        // Between 10 and 20 the first key's easing is held: nothing moves until the second key is there.
        QCOMPARE(motion.poseAt(15), at(100, 0, 90, 1, 1));
        QCOMPARE(motion.poseAt(20), at(200, 50, 270, 4, 0));
        // The scale goes by ratios (from 1 to 4, 2 halfway), the opacity straight, within 0..1.
        doc::CelMotion scale;
        scale.setKey(0, at(0, 0, 0, 1, 1), doc::Easing::linear());
        scale.setKey(10, at(0, 0, 0, 4, 0), doc::Easing::linear());
        QVERIFY(std::abs(scale.poseAt(5).scaleX - 2.0) < 1e-9);
        QVERIFY(std::abs(scale.poseAt(5).opacity - 0.5) < 1e-9);
        // An ease in-out starts and ends slowly.
        doc::CelMotion eased;
        eased.setKey(0, at(0, 0), doc::Easing::easeInOut());
        eased.setKey(10, at(100, 0), doc::Easing::linear());
        QVERIFY(eased.poseAt(1).x < 5);
        QVERIFY(eased.poseAt(9).x > 95);
        QVERIFY(std::abs(eased.poseAt(5).x - 50) < 1e-6);
    }

    void keysAreSetMovedAndTakenAway()
    {
        doc::CelMotion motion;
        motion.setKey(5, at(1, 1));
        QCOMPARE(motion.keyAt(5)->easing, doc::Easing::easeInOut()); // the first easing
        QVERIFY(motion.setEasing(5, doc::Easing::hold()));
        motion.setKey(5, at(2, 2)); // replaced, keeping the easing it had
        QCOMPARE(motion.keyAt(5)->pose.x, 2.0);
        QCOMPARE(motion.keyAt(5)->easing, doc::Easing::hold());
        QVERIFY(!motion.setEasing(6, doc::Easing::linear()));
        motion.setKey(9, at(9, 9));
        QVERIFY(!motion.moveKey(5, 9)); // a place taken
        QVERIFY(!motion.moveKey(4, 8)); // a key that is not there
        QVERIFY(motion.moveKey(5, 7));
        QVERIFY(motion.keyAt(7) && !motion.keyAt(5));
        QVERIFY(motion.moveKey(7, 7));
        QVERIFY(motion.removeKey(7));
        QVERIFY(!motion.removeKey(7));
        QCOMPARE(motion.keys().size(), size_t(1));
    }

    void aPoseTurnsAndScalesAroundThePivot()
    {
        // The pivot stays where it is (plus the move); a point one pixel to its right, turned a quarter clockwise on a
        // page whose y goes down, ends up one below it.
        const double px = 100, py = 60;
        double x = 0, y = 0;
        doc::CelPose turn = at(0, 0, 90);
        turn.matrix(px, py).map(px, py, x, y);
        QVERIFY(std::abs(x - px) < 1e-9 && std::abs(y - py) < 1e-9);
        turn.matrix(px, py).map(px + 1, py, x, y);
        QVERIFY(std::abs(x - px) < 1e-9 && std::abs(y - (py + 1)) < 1e-9);
        // Moved, then: the pivot goes to where the move says.
        at(30, -10).matrix(px, py).map(px, py, x, y);
        QVERIFY(std::abs(x - 130) < 1e-9 && std::abs(y - 50) < 1e-9);
        // Scaled by 2 around it, a point 10 to the right is 20 to the right.
        at(0, 0, 0, 2).matrix(px, py).map(px + 10, py, x, y);
        QVERIFY(std::abs(x - (px + 20)) < 1e-9 && std::abs(y - py) < 1e-9);
        QVERIFY(at(0, 0).movesNothing());
        QVERIFY(!at(0, 0, 1).movesNothing());
        QVERIFY(at(0, 0, 0, 1, 0.2).movesNothing()); // only the opacity: nothing to move
    }

    void theControllerKeepsKeysAsUndoableSteps()
    {
        document::DocumentController controller;
        controller.setSceneLength(48);
        const doc::CelId cel = controller.currentCel();
        QSignalSpy changed(&controller, &document::DocumentController::motionChanged);
        QVERIFY(controller.celMotion(cel)->isEmpty());
        QVERIFY(controller.setCelKey(cel, 0, at(0, 0)));
        QCOMPARE(changed.size(), 1);
        // The first key puts the pivot at the middle of the page.
        QCOMPARE(controller.celMotion(cel)->pivotX, controller.document().width() / 2.0);
        QCOMPARE(controller.celMotion(cel)->pivotY, controller.document().height() / 2.0);
        QVERIFY(!controller.setCelKey(cel, 0, at(0, 0))); // nothing changes
        QVERIFY(controller.setCelKey(cel, 12, at(100, 40, 30, 1.5, 0.5)));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        QCOMPARE(controller.celPoseAt(cel, 12), at(100, 40, 30, 1.5, 0.5));
        QVERIFY(controller.setCelKeyEasing(cel, 12, doc::Easing::hold()));
        QVERIFY(!controller.setCelKeyEasing(cel, 13, doc::Easing::hold())); // no key there
        QVERIFY(controller.moveCelKey(cel, 12, 20));
        QVERIFY(!controller.moveCelKey(cel, 12, 30));
        QVERIFY(controller.celMotion(cel)->keyAt(20));
        QVERIFY(controller.setCelPivot(cel, 10, 20));
        QCOMPARE(controller.celMotion(cel)->pivotX, 10.0);
        QVERIFY(!controller.setCelPivot(cel, 10, 20));
        QVERIFY(controller.removeCelKey(cel, 20));
        QVERIFY(!controller.removeCelKey(cel, 20));
        // Back through all of it, and forward again.
        while (controller.canUndo() && controller.undoText() == QStringLiteral("Undo keyframes"))
            controller.undo();
        QVERIFY(controller.celMotion(cel)->isEmpty());
        controller.redo();
        QVERIFY(!controller.celMotion(cel)->isEmpty());
        QVERIFY(controller.clearCelMotion(cel));
        QVERIFY(controller.celMotion(cel)->isEmpty());
        QVERIFY(!controller.clearCelMotion(cel));
        QVERIFY(!controller.setCelKey(doc::CelId(9999), 0, at(1, 1))); // no such cel
    }

    void aMovedCelIsDrawnAtItsPose()
    {
        doc::Document document(200, 120);
        document.setFrameCount(30);
        document.setPaperColor(0xffffffff);
        const doc::CelId piece = addSquare(document, 40, 40, 20); // red, x 40..59, y 40..59
        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(50, 50), QColor(Qt::red)); // no keys: where it is drawn

        doc::CelMotion motion;
        motion.pivotX = 50;
        motion.pivotY = 50;
        motion.setKey(0, at(0, 0), doc::Easing::linear());
        motion.setKey(10, at(100, 0), doc::Easing::linear());
        motion.setKey(20, at(100, 0, 0, 1, 0.5), doc::Easing::linear());
        document.cel(piece)->setMotion(motion);
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(50, 50), QColor(Qt::red)); // frame 0: on its place
        renderer.renderAll(document, 5);
        QCOMPARE(renderer.image().pixelColor(50, 50), QColor(Qt::white)); // half way: 50 px on
        QCOMPARE(renderer.image().pixelColor(100, 50), QColor(Qt::red));
        renderer.renderAll(document, 10);
        QCOMPARE(renderer.image().pixelColor(150, 50), QColor(Qt::red));
        QCOMPARE(renderer.image().pixelColor(100, 50), QColor(Qt::white));
        renderer.renderAll(document, 20); // half see-through, over the paper
        const QColor faded = renderer.image().pixelColor(150, 50);
        QVERIFY(faded.red() == 255 && faded.green() > 110 && faded.green() < 145);
        // A part of the frame, as a stroke redraws it, agrees with the whole.
        renderer.renderAll(document, 5);
        const QImage whole = renderer.image();
        canvas::FrameRenderer part;
        part.renderAll(document, 0);
        part.render(document, 5, QRect(80, 30, 60, 50));
        for (int y = 30; y < 80; ++y)
            for (int x = 80; x < 140; ++x)
                QCOMPARE(part.image().pixel(x, y), whole.pixel(x, y));
    }

    void aTurnedCelTurnsAroundItsPivotAndScalesFromIt()
    {
        doc::Document document(200, 200);
        document.setFrameCount(10);
        const doc::CelId piece = addSquare(document, 100, 90, 20); // x 100..119, y 90..109; the pivot at its left middle
        doc::CelMotion motion;
        motion.pivotX = 100;
        motion.pivotY = 100;
        motion.setKey(0, at(0, 0, 90), doc::Easing::hold()); // a quarter turn clockwise
        document.cel(piece)->setMotion(motion);
        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        // The square's right part (x 100..119 at y 100) goes down from the pivot: x 90..110, y 100..119.
        QCOMPARE(renderer.image().pixelColor(100, 115), QColor(Qt::red));
        QCOMPARE(renderer.image().pixelColor(115, 100), QColor(Qt::white));
        // Scaled by 2 around the pivot (a key held on).
        motion.setKey(0, at(0, 0, 0, 2));
        document.cel(piece)->setMotion(motion);
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(135, 100), QColor(Qt::red)); // 100..140 now
        QCOMPARE(renderer.image().pixelColor(145, 100), QColor(Qt::white));
        // Shown as drawn, the pose is not applied (to draw on it where its pixels are).
        renderer.setCelAsDrawn(piece);
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(125, 100), QColor(Qt::white));
        QCOMPARE(renderer.image().pixelColor(110, 100), QColor(Qt::red));
        QCOMPARE(renderer.celAsDrawn(), piece);
    }

    void theFiltersReadAMovedCelEachInTheirWay()
    {
        doc::Document document(120, 80);
        document.setFrameCount(5);
        const doc::CelId piece = addSquare(document, 20, 20, 16, 0, 0xff000000u); // black on the white paper
        doc::CelMotion motion;
        motion.pivotX = 28;
        motion.pivotY = 28;
        motion.setKey(0, at(40.5, 0.5, 0, 3), doc::Easing::hold()); // off the pixel grid, three times as big
        document.cel(piece)->setMotion(motion);
        QImage out[canvas::kResampleFilters];
        for (int f = 0; f < canvas::kResampleFilters; ++f) {
            canvas::FrameRenderer renderer;
            renderer.setResampleFilter(canvas::ResampleFilter(f));
            renderer.renderAll(document, 0);
            out[f] = renderer.image();
            QCOMPARE(out[f].pixelColor(68, 28), QColor(Qt::black)); // the middle of the piece, whichever filter
            QCOMPARE(out[f].pixelColor(110, 70), QColor(Qt::white));
        }
        // The edge is a hard one with nearest neighbor, and a ramp with the others.
        auto grays = [&](int f) {
            int n = 0;
            for (int x = 0; x < 120; ++x) {
                const int v = out[f].pixelColor(x, 28).red();
                n += v > 10 && v < 245;
            }
            return n;
        };
        QCOMPARE(grays(int(canvas::ResampleFilter::Nearest)), 0);
        QVERIFY(grays(int(canvas::ResampleFilter::Bilinear)) > 0);
        QVERIFY(grays(int(canvas::ResampleFilter::Lanczos3)) > 0);
    }

    // The Transform tool on a cel that has keys: a drag of the box changes the pose of the cel on the current frame (the key
    // there, one undo step for the drag; on a frame with no key a pose that goes with the frame unless the Cel key button makes
    // it a key); the handle above turns it around the pivot, a corner scales it from there. With no keys it moves pixels.
    void theTransformToolOnACelWithKeysMovesTurnsAndScalesIt()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece")); // x 930..989, y 520..559; pivot at its middle (960, 540)
        const doc::CelId cel = controller.currentCel();
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Transform);
        QVERIFY(!state.keyframeMode()); // no keys: the tool works on the pixels
        controller.toggleCelKey(cel);   // the first key, at the pose it has (as drawn): the cel is a piece now
        QVERIFY(state.keyframeMode());
        const QTransform toView = canvas.view().documentToView();
        const double zoom = canvas.view().zoom();
        auto at = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1));

        // On the key, inside the box: the key moves by as much as the pen moved.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(960, 540));
        QTest::mouseMove(&canvas, at(1000, 560));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1000, 560));
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1));
        const doc::CelPose moved = controller.celPoseAt(cel, 0);
        QVERIFY(std::abs(moved.x - 40) < 1.5 / zoom + 0.6 && std::abs(moved.y - 20) < 1.5 / zoom + 0.6);
        QCOMPARE(controller.celMotion(cel)->pivotX, 960.0); // the middle of what it shows
        QCOMPARE(controller.celMotion(cel)->pivotY, 540.0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        QCOMPARE(canvas.documentImage().pixelColor(1000, 560), QColor(Qt::red));
        QCOMPARE(canvas.documentImage().pixelColor(935, 525), QColor(Qt::white));
        controller.undo(); // the whole drag is one step
        QCOMPARE(controller.celPoseAt(cel, 0).x, 0.0);
        QCOMPARE(canvas.documentImage().pixelColor(935, 525), QColor(Qt::red));
        controller.redo();

        // A frame with no key: the pose is there while the frame is, and goes with it.
        controller.setCurrentFrame(10);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(1000, 560));
        QTest::mouseMove(&canvas, at(1100, 560));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1100, 560));
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1)); // not a key
        QVERIFY(controller.celPoseAt(cel, 10).x > 120);
        QCOMPARE(canvas.documentImage().pixelColor(1100, 560), QColor(Qt::red));
        const double temporary = controller.celPoseAt(cel, 10).x;
        controller.setCurrentFrame(0);
        controller.setCurrentFrame(10);
        QCOMPARE(controller.celPoseAt(cel, 10).x, moved.x); // all as before: it went when the frame was left
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1));

        // The Cel key button before leaving makes it a key.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(1000, 560));
        QTest::mouseMove(&canvas, at(1100, 560));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1100, 560));
        controller.toggleCelKey(cel);
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(2));
        QCOMPARE(controller.celPoseAt(cel, 10).x, temporary);
        controller.setCurrentFrame(0);
        controller.setCurrentFrame(10);
        QCOMPARE(controller.celPoseAt(cel, 10).x, temporary); // it stayed
        const double midway = controller.celPoseAt(cel, 5).x;
        QVERIFY(midway > controller.celPoseAt(cel, 0).x && midway < temporary);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        controller.undo(); // the key made
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1));
        controller.undo(); // the drag of the first key
        controller.undo(); // the first key itself
        QVERIFY(controller.celMotion(cel)->isEmpty());
        QVERIFY(!state.keyframeMode()); // no keys: pixels again
        controller.setCurrentFrame(0);
        controller.toggleCelKey(cel);

        // The handle above the box turns it around the pivot: a quarter turn, the pen going from over the pivot to its right.
        controller.setCurrentFrame(0);
        const double reach = 30.0 / zoom; // the handle's distance from the top side
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(960, 520 - reach));
        QTest::mouseMove(&canvas, at(960 + 20 + reach, 540));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(960 + 20 + reach, 540));
        QVERIFY2(std::abs(controller.celPoseAt(cel, 0).rotation - 90) < 6, qPrintable(QString::number(controller.celPoseAt(cel, 0).rotation)));
        controller.undo();

        // A corner scales it from the pivot: twice as far out, twice as big.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(990, 560));
        QTest::mouseMove(&canvas, at(960 + 2 * 30, 540 + 2 * 20));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(960 + 2 * 30, 540 + 2 * 20));
        QVERIFY2(std::abs(controller.celPoseAt(cel, 0).scaleX - 2.0) < 0.1, qPrintable(QString::number(controller.celPoseAt(cel, 0).scaleX)));
        QCOMPARE(controller.celPoseAt(cel, 0).scaleX, controller.celPoseAt(cel, 0).scaleY);
        controller.undo();

        // Outside the box nothing happens, and with a drawing tool the cel is shown as it is drawn, not posed.
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, at(100, 100));
        QCOMPARE(controller.celPoseAt(cel, 0), doc::CelPose{});
        controller.setCelKey(cel, 0, [] { doc::CelPose p; p.x = 300; return p; }());
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::red)); // posed (the tool moves the cel)
        state.setActiveTool(tools::ToolId::Brush);
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::white)); // as drawn
        QCOMPARE(canvas.documentImage().pixelColor(960, 540), QColor(Qt::red));
        state.setActiveTool(tools::ToolId::Transform);
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::red));
        controller.removeCelKey(cel, 0); // no keys: the same tool is on the pixels, the cel is as drawn
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::white));
    }

    // A pose edit is open, as the Transform tool's: the bar's OK (Enter) applies it as one undo step, Cancel (Esc) puts the
    // cel back, and leaving the frame applies it.
    void aPoseEditStaysOpenForOkOrCancel()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece"));
        const doc::CelId cel = controller.currentCel();
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const QTransform toView = canvas.view().documentToView();
        auto at = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        state.setActiveTool(tools::ToolId::Transform);
        controller.toggleCelKey(cel);
        QVERIFY(canvas.transformBounds().isNull());
        auto drag = [&](double fromX, double toX) {
            QTest::mousePress(&canvas, Qt::LeftButton, {}, at(fromX, 540));
            QTest::mouseMove(&canvas, at(toX, 540));
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(toX, 540));
        };
        drag(960, 1000); // two drags, one edit
        drag(1000, 1040);
        QVERIFY(!canvas.transformBounds().isNull()); // the bar has a place
        const double moved = controller.celPoseAt(cel, 0).x;
        QVERIFY(moved > 70);
        canvas.finishTransform(false); // Cancel
        QVERIFY(canvas.transformBounds().isNull());
        QCOMPARE(controller.celPoseAt(cel, 0).x, 0.0);
        drag(960, 1000);
        drag(1000, 1040);
        canvas.finishTransform(true); // OK: one step for both drags
        QVERIFY(canvas.transformBounds().isNull());
        QCOMPARE(controller.celPoseAt(cel, 0).x, moved);
        controller.undo();
        QCOMPARE(controller.celPoseAt(cel, 0).x, 0.0);
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1)); // the key stays: it was one step before
        // Leaving the frame applies an edit that was open.
        drag(960, 1000);
        QVERIFY(!canvas.transformBounds().isNull());
        controller.setCurrentFrame(3);
        QVERIFY(canvas.transformBounds().isNull());
        QVERIFY(controller.celPoseAt(cel, 0).x > 30);
        controller.undo();
        QCOMPARE(controller.celPoseAt(cel, 0).x, 0.0);
    }

    // The Move tool on a cel that has keys moves the cel as a whole by a drag anywhere on the canvas (no handles): the key
    // of the frame, or a pose that goes with the frame; with no keys it moves the pixels as ever.
    void theMoveToolOnACelWithKeysMovesTheCel()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece")); // x 930..989, y 520..559
        const doc::CelId cel = controller.currentCel();
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const QTransform toView = canvas.view().documentToView();
        const double zoom = canvas.view().zoom();
        auto at = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        state.setActiveTool(tools::ToolId::Move);
        controller.toggleCelKey(cel);
        QVERIFY(state.keyframeMode());
        // A drag that starts away from the piece moves it all the same.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(700, 300));
        QTest::mouseMove(&canvas, at(780, 330));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(780, 330));
        QVERIFY(controller.celMotion(cel)->keyAt(0));
        const doc::CelPose pose = controller.celPoseAt(cel, 0);
        QVERIFY(std::abs(pose.x - 80) < 1.5 / zoom + 0.6 && std::abs(pose.y - 30) < 1.5 / zoom + 0.6);
        QCOMPARE(pose.rotation, 0.0);
        QCOMPARE(pose.scaleX, 1.0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        QCOMPARE(canvas.documentImage().pixelColor(1030, 560), QColor(Qt::red)); // drawn there
        QCOMPARE(controller.document().cel(cel)->drawingAt(0)->layers.front().image.pixel(935, 525), doc::Pixel(0xffff0000u)); // pixels as they were
        // A cel with no drawing on the frame: nothing to move, and the tool says so.
        controller.removeCelKey(cel, 0); // no keys: the pixels, the move tool as ever
        QVERIFY(!state.keyframeMode());
        QCOMPARE(canvas.documentImage().pixelColor(1030, 560), QColor(Qt::white));
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(960, 540));
        QTest::mouseMove(&canvas, at(1000, 540));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1000, 540));
        QVERIFY(controller.celMotion(cel)->isEmpty()); // no key made: the pixels moved
        QCOMPARE(qAlpha(controller.document().cel(cel)->drawingAt(0)->layers.front().image.pixel(935, 525)), 0);
    }

    // The onion skin shows the ghosts of the other frames as the cel is on each of them: posed by its keys, unless it is shown
    // as drawn (to line pixels up).
    void theOnionSkinFollowsTheKeys()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece")); // x 930..989, y 520..559, one drawing for the scene
        const doc::CelId cel = controller.currentCel();
        doc::CelPose far;
        far.x = 300; // key 0 at the drawn place, key 10 three hundred pixels on: frame 7 is at x 210
        controller.setCelKey(cel, 0, doc::CelPose{});
        controller.setCelKey(cel, 10, far);
        controller.setCelKeyEasing(cel, 0, doc::Easing::linear());
        tools::OnionSkinSettings onion;
        onion.enabled = true;
        onion.before = 0;
        onion.after = 2;
        onion.afterColor = QColor(0, 0, 255);
        onion.opacity = 1.0;
        state.setOnionSkin(onion);
        // The drawing is held for all the frames: there is no ghost of another drawing. Give frame 6 and 7 drawings of
        // their own (the same square), so there are ghosts.
        controller.execute(doc::makeSplitDrawing(controller.documentForCommands(), cel, 6).command);
        controller.execute(doc::makeSplitDrawing(controller.documentForCommands(), cel, 7).command);
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Transform);
        controller.setCurrentFrame(5); // pose x 150; the ghosts are of frames 6 and 7: x 180 and 210
        QTest::qWait(50);
        const QTransform toView = canvas.view().documentToView();
        auto bluish = [&](double docX) {
            const QColor c = canvas.grab().toImage().pixelColor(toView.map(QPointF(docX, 540)).toPoint());
            return c.blue() > c.red() + 20; // the ghost's tint over the paper
        };
        QVERIFY(bluish(1180)); // the ghost of frame 7, where the keys put the cel then (1140..1199)
        QVERIFY(!bluish(1250));
        // As drawn (a pixel tool): the ghosts are where the pixels are.
        state.setActiveTool(tools::ToolId::Brush);
        QTest::qWait(50);
        QVERIFY(!bluish(1180));
        QVERIFY(bluish(960)); // the frame's own place (930..989), where the ghost is drawn
    }

    // The options of Transform and Move, in keyframe mode, hold the pose of the current cel on the current frame; editing a field makes a key.
    void theKeyframeOptionsShowAndEditThePose()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        ui::ToolPropertiesPanel panel(&state);
        panel.setDocument(&controller);
        panel.show();
        QVERIFY(!panel.findChild<QCheckBox *>(QStringLiteral("keyframeMode"))); // nothing to switch on: the keys decide
        state.setActiveTool(tools::ToolId::Move);
        QVERIFY(!panel.findChild<QDoubleSpinBox *>(QStringLiteral("poseX"))->isVisible()); // no keys: the pixels, no pose
        state.setKeyframeMode(true); // what the canvas does when the cel has keys
        auto *x = panel.findChild<QDoubleSpinBox *>(QStringLiteral("poseX"));
        auto *rotation = panel.findChild<QDoubleSpinBox *>(QStringLiteral("poseRotation"));
        auto *scale = panel.findChild<QDoubleSpinBox *>(QStringLiteral("poseScale"));
        auto *key = panel.findChild<QPushButton *>(QStringLiteral("poseKey"));
        auto *del = panel.findChild<QPushButton *>(QStringLiteral("poseDelete"));
        QVERIFY(x && rotation && scale && key && del);
        const doc::CelId cel = controller.currentCel();
        QVERIFY(x->isVisible());
        QCOMPARE(scale->value(), 100.0);
        QVERIFY(key->isEnabled() && !del->isEnabled()); // no key here yet
        key->click(); // the first one
        QVERIFY(controller.celMotion(cel)->keyAt(0));
        QVERIFY(!key->isEnabled() && del->isEnabled());
        x->setValue(75); // on a key: the key changes
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1));
        QCOMPARE(controller.celPoseAt(cel, 0).x, 75.0);
        controller.setCurrentFrame(12);
        QCOMPARE(x->value(), 75.0); // after the last key it stays as it was put
        rotation->setValue(45); // on a frame with no key: a pose that goes with the frame
        scale->setValue(150);
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1));
        QCOMPARE(controller.celPoseAt(cel, 12).rotation, 45.0);
        QCOMPARE(controller.celPoseAt(cel, 12).scaleX, 1.5);
        key->click(); // made a key before leaving
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(2));
        QCOMPARE(controller.celPoseAt(cel, 12).rotation, 45.0);
        controller.setCurrentFrame(6);
        QVERIFY(x->value() == 75.0 && rotation->value() > 0 && rotation->value() < 45); // between: in between
        QVERIFY(!del->isEnabled());
        controller.setCurrentFrame(12);
        del->click();
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(1));
        // Changing the key's curve.
        controller.setCurrentFrame(0);
        auto *easing = panel.findChild<QComboBox *>(QStringLiteral("poseEasing"));
        easing->setCurrentIndex(easing->findText(QStringLiteral("Hold")));
        emit easing->activated(easing->currentIndex());
        QCOMPARE(controller.celMotion(cel)->keyAt(0)->easing, doc::Easing::hold());
    }

    // The "Cel key" button of the timeline, and the row of keys that comes with the first one: a click on a diamond shows
    // its frame, a drag moves it, a double click on the row makes a key, and the cel's own row is not touched.
    void theTimelineHasARowOfKeys()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(40);
        const doc::CelId cel = controller.currentCel();
        ui::TimelinePanel panel(&controller, &state);
        panel.resize(900, 260);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *button = panel.findChild<QToolButton *>(QStringLiteral("celKey"));
        QVERIFY(button);
        QVERIFY(!button->isChecked());
        ui::TimelineView *view = panel.view();
        ui::TimelineMetrics m;
        const int cels = controller.document().celCount();
        controller.setCurrentFrame(4);
        button->click(); // a key here
        QVERIFY(button->isChecked());
        QVERIFY(controller.celMotion(cel)->keyAt(4));
        // The row of keys is under the cel's: one row more.
        const int motionY = m.rowY(1) + m.rowHeight / 2;
        auto diamond = [&](int frame) { return QPoint(m.frameX(frame) + m.frameWidth / 2, motionY); };
        auto *vp = view->viewport();
        QVERIFY(cels == 1);
        controller.setCurrentFrame(0);
        QTest::mouseClick(vp, Qt::LeftButton, {}, diamond(4));
        QCOMPARE(controller.currentFrame(), 4); // a click on a key shows its frame
        // Dragged: the key moves, one undo step.
        QTest::mousePress(vp, Qt::LeftButton, {}, diamond(4));
        QTest::mouseMove(vp, diamond(7));
        QTest::mouseMove(vp, diamond(9));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, diamond(9));
        QVERIFY(controller.celMotion(cel)->keyAt(9) && !controller.celMotion(cel)->keyAt(4));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        // A double click on an empty cell of the row: a key there.
        QMouseEvent press(QEvent::MouseButtonDblClick, QPointF(diamond(20)), vp->mapToGlobal(QPointF(diamond(20))),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(diamond(20)), vp->mapToGlobal(QPointF(diamond(20))),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &release);
        QVERIFY(controller.celMotion(cel)->keyAt(20));
        // The header of the row of keys has no toggles: a click on it chooses the cel and changes nothing else.
        const bool visible = controller.document().cel(cel)->properties().visible;
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(60, motionY));
        QCOMPARE(controller.document().cel(cel)->properties().visible, visible);
        QCOMPARE(controller.currentCel(), cel);
        // The arrow at the left of the keys' header folds the row away under its cel (the keys stay); the one in the cel's
        // header at the right brings it back.
        QVERIFY(view->motionRowShown(0));
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(18, motionY));
        QVERIFY(!view->motionRowShown(0));
        QCOMPARE(controller.celMotion(cel)->keys().size(), size_t(2));
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(m.headerWidth - 12, m.rowY(0) + m.rowHeight / 2));
        QVERIFY(view->motionRowShown(0)); // it is back
        QCOMPARE(controller.document().cel(cel)->properties().visible, visible); // the arrow is no toggle of the cel
        // The button takes the key of the current frame away, and the row goes with the last key.
        controller.setCurrentFrame(9);
        button->click();
        QVERIFY(!controller.celMotion(cel)->keyAt(9));
        controller.setCurrentFrame(20);
        button->click();
        QVERIFY(controller.celMotion(cel)->isEmpty());
    }
};

QTEST_MAIN(TestCelMotion)
#include "tst_celmotion.moc"
