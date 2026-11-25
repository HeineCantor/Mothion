#include "canvas/CanvasView.h"
#include "canvas/FrameRenderer.h"
#include "canvas/VectorRender.h"
#include "core/deform/BoneField.h"
#include "core/deform/CurveField.h"
#include "core/document/CelMotion.h"
#include "core/document/Document.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "document/DocumentController.h"
#include "io/FrameExport.h"
#include "io/ProjectFile.h"
#include "tools/ToolState.h"
#include "ui/panels/ToolPropertiesPanel.h"
#include "ui/AutoplayWindow.h"
#include "ui/TrajectoryMenu.h"
#include "ui/panels/timeline/TimelinePanel.h"
#include "ui/panels/timeline/TimelineView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <set>

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

// A key of every channel at once, as the whole pose of the frame.
void keyAll(doc::CelMotion &motion, int frame, const doc::CelPose &pose)
{
    for (doc::Channel c : doc::kChannels)
        motion.setKey(c, frame, pose);
}
void keyAll(doc::CelMotion &motion, int frame, const doc::CelPose &pose, const doc::Easing &easing)
{
    for (doc::Channel c : doc::kChannels)
        motion.setKey(c, frame, pose, easing);
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
        keyAll(motion, 10, at(100, 0, 90, 1, 1), doc::Easing::hold()); // (its easing is the one from 10 to 20)
        keyAll(motion, 20, at(200, 50, 270, 4, 0), doc::Easing::linear());
        keyAll(motion, 0, at(0, 0, 0, 1, 1), doc::Easing::linear());
        QCOMPARE(motion.keyFrames().size(), size_t(3));
        QCOMPARE(motion.keyFrames().front(), 0); // by frame, whatever the order they came in
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
        keyAll(scale, 0, at(0, 0, 0, 1, 1), doc::Easing::linear());
        keyAll(scale, 10, at(0, 0, 0, 4, 0), doc::Easing::linear());
        QVERIFY(std::abs(scale.poseAt(5).scaleX - 2.0) < 1e-9);
        QVERIFY(std::abs(scale.poseAt(5).opacity - 0.5) < 1e-9);
        // An ease in-out starts and ends slowly.
        doc::CelMotion eased;
        keyAll(eased, 0, at(0, 0), doc::Easing::easeInOut());
        keyAll(eased, 10, at(100, 0), doc::Easing::linear());
        QVERIFY(eased.poseAt(1).x < 5);
        QVERIFY(eased.poseAt(9).x > 95);
        QVERIFY(std::abs(eased.poseAt(5).x - 50) < 1e-6);
    }

    void keysAreSetMovedAndTakenAway()
    {
        doc::CelMotion motion;
        keyAll(motion, 5, at(1, 1));
        QCOMPARE(motion.keyAt(doc::Channel::Position, 5)->easing, doc::Easing::easeInOut()); // the first easing
        QVERIFY(motion.setEasingAll(5, doc::Easing::hold()));
        keyAll(motion, 5, at(2, 2)); // replaced, keeping the easing it had
        QCOMPARE(motion.keyAt(doc::Channel::Position, 5)->pose.x, 2.0);
        QCOMPARE(motion.keyAt(doc::Channel::Position, 5)->easing, doc::Easing::hold());
        QVERIFY(!motion.setEasingAll(6, doc::Easing::linear()));
        keyAll(motion, 9, at(9, 9));
        QVERIFY(!motion.moveKeysAt(5, 9)); // a place taken
        QVERIFY(!motion.moveKeysAt(4, 8)); // a key that is not there
        QVERIFY(motion.moveKeysAt(5, 7));
        QVERIFY(motion.hasKeyAt(7) && !motion.hasKeyAt(5));
        QVERIFY(motion.moveKeysAt(7, 7));
        QVERIFY(motion.removeKeysAt(7));
        QVERIFY(!motion.removeKeysAt(7));
        QCOMPARE(motion.keyFrames().size(), size_t(1));
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

    void aDeformerBendsTheCelThatFollowsIt()
    {
        doc::Document document(200, 200);
        document.setFrameCount(30);
        document.setPaperColor(0xffffffff);
        // A red bar along a bone from (20, 100) to (120, 100).
        const doc::CelId piece = addSquare(document, 20, 97, 6);
        {
            doc::Cel &cel = *document.cel(piece);
            auto &image = cel.drawing(cel.drawings().begin()->first)->layers.front().image;
            for (int x = 20; x < 120; ++x)
                for (int y = 97; y < 103; ++y)
                    image.setPixel(x, y, 0xffff0000u);
        }
        doc::CelProperties b{"Bones"};
        b.deformer = doc::DeformerKind::Bones;
        doc::Cel &bones = document.insertCel(1, std::make_unique<doc::Cel>(document.allocateCelId(), b));
        doc::Deformer deformer;
        deformer.bones.push_back({-1, 20, 10});
        deformer.rest = {20, 100, 0, 100};
        bones.setDeformer(deformer);
        document.cel(piece)->setProperties([&] { auto p = document.cel(piece)->properties(); p.parent = bones.id(); return p; }());

        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(100, 100), QColor(Qt::red)); // at rest: as drawn
        // Turned down by a key on frame 10.
        doc::CelMotion motion;
        doc::CelPose rest, down;
        rest.deform = deformer.rest;
        down.deform = {20, 100, 90, 100};
        motion.setKey(doc::Channel::Deform, 0, rest, doc::Easing::linear());
        motion.setKey(doc::Channel::Deform, 10, down, doc::Easing::linear());
        bones.setMotion(motion);
        renderer.renderAll(document, 10);
        QCOMPARE(renderer.image().pixelColor(100, 100), QColor(Qt::white));
        QCOMPARE(renderer.image().pixelColor(20, 180), QColor(Qt::red));
        QCOMPARE(renderer.image().pixelColor(20, 120), QColor(Qt::red));
        // A part of the frame agrees with the whole.
        const QImage whole = renderer.image();
        canvas::FrameRenderer part;
        part.renderAll(document, 0);
        part.render(document, 10, QRect(0, 90, 60, 100));
        for (int y = 90; y < 190; ++y)
            for (int x = 0; x < 60; ++x)
                QCOMPARE(part.image().pixel(x, y), whole.pixel(x, y));
        // Shown undeformed (to draw on it), or with its deformer inactive: as drawn.
        renderer.setCelUndeformed(piece);
        renderer.renderAll(document, 10);
        QCOMPARE(renderer.image().pixelColor(100, 100), QColor(Qt::red));
        renderer.setCelUndeformed(doc::kNoId);
        bones.setProperties([&] { auto p = bones.properties(); p.visible = false; return p; }());
        renderer.renderAll(document, 10);
        QCOMPARE(renderer.image().pixelColor(100, 100), QColor(Qt::red));
    }

    void aVectorLayerIsBentStrokeByStroke()
    {
        tools::ToolState state;
        doc::Document document(200, 200);
        document.setFrameCount(30);
        document.setPaperColor(0xffffffff);
        const doc::CelId piece = addSquare(document, 0, 0, 0);
        doc::Cel &cel = *document.cel(piece);
        doc::Layer &layer = cel.drawing(cel.drawings().begin()->first)->layers.front();
        layer.kind = doc::LayerKind::Vector;
        doc::VectorStroke line;
        line.size = 6;
        line.color = 0xff000000u;
        for (int x = 20; x <= 120; x += 4) {
            brush::BrushInput in;
            in.x = x;
            in.y = 100;
            line.points.push_back(in);
        }
        layer.strokes = {line};
        canvas::drawStrokes(layer.image, layer.strokes, state, {});
        doc::CelProperties b{"Bones"};
        b.deformer = doc::DeformerKind::Bones;
        doc::Cel &bones = document.insertCel(1, std::make_unique<doc::Cel>(document.allocateCelId(), b));
        doc::Deformer deformer;
        deformer.bones.push_back({-1, 20, 10});
        deformer.rest = {20, 100, 0, 100};
        bones.setDeformer(deformer);
        cel.setProperties([&] { auto p = cel.properties(); p.parent = bones.id(); return p; }());
        doc::CelMotion motion;
        doc::CelPose down;
        down.deform = {20, 100, 90, 100};
        motion.setKey(doc::Channel::Deform, 0, down);
        bones.setMotion(motion);

        // How wide the line is across y = 160, and how dark its middle.
        auto across = [](const QImage &image, int y, int *darkest) {
            int wide = 0;
            *darkest = 255;
            for (int x = 0; x < 60; ++x) {
                const int v = qGray(image.pixel(x, y));
                *darkest = std::min(*darkest, v);
                wide += v < 128 ? 1 : 0;
            }
            return wide;
        };
        canvas::FrameRenderer renderer;
        renderer.setBrushes(&state);
        renderer.renderAll(document, 0);
        int darkest = 0;
        const int wide = across(renderer.image(), 160, &darkest);
        QVERIFY2(wide >= 4 && wide <= 8, qPrintable(QString::number(wide))); // as wide as drawn, down the bent bone
        QVERIFY(darkest < 40);
        QCOMPARE(renderer.image().pixelColor(100, 100), QColor(Qt::white)); // not where it was
        // The strokes themselves are not touched.
        QCOMPARE(layer.strokes.front().points.back().x, 120.0);
        // Without the brushes: its pixels bent, at the same place.
        canvas::FrameRenderer pixels;
        pixels.renderAll(document, 0);
        QVERIFY(across(pixels.image(), 160, &darkest) >= 3);
    }

    void theControllerMakesBonesAsUndoableSteps()
    {
        document::DocumentController controller;
        controller.setSceneLength(48);
        const doc::CelId drawn = controller.currentCel();
        controller.addDeformer(doc::DeformerKind::Bones);
        const doc::CelId bones = controller.currentCel();
        QVERIFY(bones != drawn);
        const doc::Document &d = controller.document();
        QCOMPARE(d.cel(bones)->properties().name, std::string("Bones 1"));
        QCOMPARE(d.cel(drawn)->properties().parent, bones); // the cel that was current is bent by it
        QCOMPARE(controller.undoText(), QStringLiteral("Undo new cel"));

        QCOMPARE(controller.addBone(bones, -1, {10, 10}, {110, 10}), 0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo bones"));
        QCOMPARE(controller.addBone(bones, 0, {110, 10}, {110, 60}), 1);
        QCOMPARE(controller.addBone(drawn, -1, {0, 0}, {1, 1}), -1); // not a deformer
        QCOMPARE(controller.addBone(bones, 0, {5, 5}, {5, 5}), -1);   // no length
        const doc::Deformer &rig = d.cel(bones)->deformer();
        QCOMPARE(rig.bones.size(), std::size_t(2));
        QCOMPARE(rig.bones[1].parent, 0);
        QCOMPARE(rig.rest[4 + doc::BoneAngle], 90.0); // from its parent's
        QCOMPARE(rig.rest[4 + doc::BoneLength], 50.0);
        QVERIFY(std::abs(rig.rest[4 + doc::BoneX]) < 1e-9); // it starts at the parent's end

        // A key, then a third bone: the key gets it at rest.
        std::vector<double> pose = rig.rest;
        pose[doc::BoneAngle] = 30;
        QVERIFY(controller.setDeformKey(bones, 5, pose));
        QVERIFY(!controller.setDeformKey(bones, 5, pose)); // nothing changes
        QVERIFY(!controller.setDeformKey(drawn, 5, pose));
        QCOMPARE(controller.addBone(bones, 1, {110, 60}, {150, 60}), 2);
        QCOMPARE(d.deformPoseAt(*d.cel(bones), 5).size(), std::size_t(3 * doc::kBoneParams));
        QCOMPARE(d.deformPoseAt(*d.cel(bones), 5)[doc::BoneAngle], 30.0);
        // The Cel key of a deformer keys its pose, nothing else.
        QVERIFY(controller.setCelKey(bones, 9, controller.celPoseAt(bones, 9)));
        QVERIFY(d.cel(bones)->motion().keyAt(doc::Channel::Deform, 9));
        QVERIFY(!d.cel(bones)->motion().hasKeys(doc::Channel::Position));

        // Taking the middle bone away takes the one hanging from it too, from the keys as well.
        QVERIFY(controller.removeDeformerPart(bones, 1));
        QCOMPARE(d.cel(bones)->deformer().bones.size(), std::size_t(1));
        QCOMPARE(d.deformPoseAt(*d.cel(bones), 5).size(), std::size_t(doc::kBoneParams));
        QCOMPARE(d.deformPoseAt(*d.cel(bones), 5)[doc::BoneAngle], 30.0);
        QVERIFY(!controller.removeDeformerPart(bones, 4));
        controller.undo();
        QCOMPARE(d.cel(bones)->deformer().bones.size(), std::size_t(3));
        QCOMPARE(d.deformPoseAt(*d.cel(bones), 5).size(), std::size_t(3 * doc::kBoneParams));

        // The rest moved: same bones, other numbers.
        doc::Deformer moved = d.cel(bones)->deformer();
        moved.rest[doc::BoneX] = 20;
        QVERIFY(controller.setDeformerRest(bones, moved));
        QVERIFY(!controller.setDeformerRest(bones, moved));
        moved.bones.pop_back();
        QVERIFY(!controller.setDeformerRest(bones, moved)); // not as many bones
    }

    void curvesAndEnvelopesAreMadeOfNodes()
    {
        document::DocumentController controller;
        controller.setSceneLength(48);
        const doc::Document &d = controller.document();
        controller.addDeformer(doc::DeformerKind::Curve);
        const doc::CelId curve = controller.currentCel();
        QCOMPARE(d.cel(curve)->properties().name, std::string("Curve 1"));
        QVERIFY(d.cel(curve)->deformer().isEmpty());
        QCOMPARE(controller.addNode(curve, {100, 100}), 0);
        QCOMPARE(controller.addNode(curve, {200, 100}, 0), 1);
        QCOMPARE(controller.addNode(curve, {200, 200}, 1), 2);
        QCOMPARE(controller.addNode(curve, {300, 300}, 0), -1); // not the last node of its curve
        auto nodes = [&] { return deform::curveNodes(d.cel(curve)->deformer(), d.cel(curve)->deformer().rest); };
        QVERIFY(std::hypot(nodes()[2].at.x - 200, nodes()[2].at.y - 200) < 1e-9);
        QVERIFY(std::abs(nodes()[0].angle) < 1e-9 && nodes()[0].out > 30); // the first piece gave the first node its way
        // A key, then the middle node taken away: the last node stays where it was, in the key too.
        std::vector<double> pose = d.cel(curve)->deformer().rest;
        pose[doc::NodeA] += 50; // all of it moved right in the key
        QVERIFY(controller.setDeformKey(curve, 3, pose));
        QVERIFY(controller.removeDeformerPart(curve, 1));
        QCOMPARE(nodes().size(), std::size_t(2));
        QVERIFY(std::hypot(nodes()[1].at.x - 200, nodes()[1].at.y - 200) < 1e-9);
        const auto keyed = deform::curveNodes(d.cel(curve)->deformer(), d.deformPoseAt(*d.cel(curve), 3));
        QVERIFY(std::hypot(keyed[1].at.x - 250, keyed[1].at.y - 200) < 1e-9);
        controller.undo();
        QCOMPARE(nodes().size(), std::size_t(3));
        QCOMPARE(controller.addNode(curve, {200, 200}, 2), -1); // where the last node is: no piece

        // A curve of its own, then one more node on the first curve: it goes in after that curve's last node.
        QCOMPARE(controller.addNode(curve, {500, 500}), 3);
        QCOMPARE(controller.addNode(curve, {600, 500}, 3), 4);
        QCOMPARE(controller.addNode(curve, {300, 200}, 2), 3);
        QCOMPARE(nodes().size(), std::size_t(6));
        QVERIFY(nodes()[4].starts && !nodes()[3].starts && !nodes()[5].starts);
        QVERIFY(std::hypot(nodes()[4].at.x - 500, nodes()[4].at.y - 500) < 1e-9); // the second curve did not move
        QVERIFY(std::hypot(nodes()[5].at.x - 600, nodes()[5].at.y - 500) < 1e-9);
        QCOMPARE(deform::curveRuns(nodes()).size(), std::size_t(2));
        // Its first node taken away: the next one begins it, where it was.
        QVERIFY(controller.removeDeformerPart(curve, 4));
        QVERIFY(nodes()[4].starts);
        QVERIFY(std::hypot(nodes()[4].at.x - 600, nodes()[4].at.y - 500) < 1e-9);

        // An envelope starts round what the cel shows.
        controller.chooseCel(controller.document().celAt(0).id());
        const doc::CelId drawn = controller.currentCel();
        QString why;
        const auto target = controller.beginPaint(true, &why);
        QVERIFY2(target, qPrintable(why));
        const doc::TiledImage before = *target->image;
        for (int y = 300; y < 400; ++y)
            for (int x = 500; x < 700; ++x)
                target->image->setPixel(x, y, 0xff000000u);
        controller.endPaint(before);
        controller.addDeformer(doc::DeformerKind::Envelope);
        const doc::CelId envelope = controller.currentCel();
        const auto round = deform::curveNodes(d.cel(envelope)->deformer(), d.cel(envelope)->deformer().rest);
        QCOMPARE(round.size(), std::size_t(4));
        for (const auto &n : round) // each node out of the drawing, round it
            QVERIFY(n.at.x < 500 || n.at.x > 699 || n.at.y < 300 || n.at.y > 399);
        QCOMPARE(d.cel(drawn)->properties().parent, envelope);
    }

    void aCelIsCarriedByABoneFromTheMenusChoice()
    {
        document::DocumentController controller;
        controller.setSceneLength(48);
        const doc::CelId hand = controller.currentCel();
        controller.addDeformer(doc::DeformerKind::Bones);
        const doc::CelId bones = controller.currentCel();
        controller.addBone(bones, -1, {0, 0}, {100, 0});
        controller.addBone(bones, 0, {100, 0}, {200, 0});
        controller.addBone(bones, 1, {200, 0}, {300, 0});
        const doc::Document &d = controller.document();
        QVERIFY(controller.setDeformerParent(hand, bones, 2));
        QVERIFY(!controller.setDeformerParent(hand, bones, 2)); // nothing changes
        QVERIFY(!controller.setDeformerParent(hand, bones, 7)); // no such bone
        QCOMPARE(d.cel(hand)->properties().parentPart, 2);
        QCOMPARE(d.followedDeformer(*d.cel(hand)), nullptr);
        // The first bone taken away: the bones after it go too (they hung from it), and the hand is bent again.
        QVERIFY(controller.removeDeformerPart(bones, 1));
        QCOMPARE(d.cel(hand)->properties().parentPart, -1);
        QCOMPARE(d.followedDeformer(*d.cel(hand)), d.cel(bones));
        controller.undo();
        QCOMPARE(d.cel(hand)->properties().parentPart, 2);
        // Following a trajectory instead lets the bone go.
        controller.addTrajectory();
        const doc::CelId path = controller.currentCel();
        QVERIFY(controller.setTrajectoryParent(hand, path));
        QCOMPARE(d.cel(hand)->properties().parentPart, -1);
    }

    void theZoneOfABoneAndTheNodesOfACurveAreDragged()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addDeformer(doc::DeformerKind::Bones);
        const doc::CelId bones = controller.currentCel();
        controller.addBone(bones, -1, {600, 500}, {1000, 500});
        state.setRigMode(true);
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const QTransform toView = canvas.view().documentToView();
        auto at = [&](const QPointF &docPoint) { return toView.map(docPoint).toPoint(); };
        auto drag = [&](QPointF from, QPointF to) {
            QTest::mousePress(&canvas, Qt::LeftButton, {}, at(from));
            for (int k = 1; k <= 4; ++k)
                QTest::mouseMove(&canvas, at(from + (to - from) * k / 4.0));
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(to));
        };
        // The bone chosen: its zone's handles show; the inner one dragged out sets the radius.
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, at({800, 500}));
        const auto radius = canvas.boneGizmo().partPoint(0, canvas::BoneGizmo::Part::ZoneRadius);
        QVERIFY(radius);
        drag(*radius, QPointF(800, 500 + 90));
        QVERIFY(std::abs(controller.document().cel(bones)->deformer().bones[0].radius - 90) < 3);
        const auto fade = canvas.boneGizmo().partPoint(0, canvas::BoneGizmo::Part::ZoneFade);
        drag(*fade, QPointF(800, 500 + 150));
        QVERIFY(std::abs(controller.document().cel(bones)->deformer().bones[0].fade - 60) < 3);

        // A curve: clicks put its nodes, a handle turns its node and what comes after.
        controller.addDeformer(doc::DeformerKind::Curve);
        const doc::CelId curve = controller.currentCel();
        for (QPointF p : {QPointF(500, 400), QPointF(800, 400), QPointF(1100, 400)})
            QTest::mouseClick(&canvas, Qt::LeftButton, {}, at(p));
        QCOMPARE(controller.document().cel(curve)->deformer().bones.size(), std::size_t(3));
        // Ctrl+click: a curve of its own, and the next click goes on along it.
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ControlModifier, at({500, 800}));
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, at({800, 800}));
        {
            const auto all = deform::curveNodes(controller.document().cel(curve)->deformer(),
                                                controller.document().cel(curve)->deformer().rest);
            QCOMPARE(all.size(), std::size_t(5));
            QCOMPARE(deform::curveRuns(all).size(), std::size_t(2));
            QVERIFY(all[3].starts && !all[4].starts);
        }
        const canvas::CurveGizmo &gizmo = canvas.curveGizmo();
        const QPointF out = *gizmo.partPoint(1, canvas::CurveGizmo::Part::Out);
        const double reach = QLineF(QPointF(800, 400), out).length();
        drag(out, QPointF(800, 400 + reach)); // the tangent turned down a quarter
        const auto nodes = deform::curveNodes(controller.document().cel(curve)->deformer(),
                                              controller.document().cel(curve)->deformer().rest);
        QVERIFY(std::abs(nodes[1].angle - M_PI / 2) < 0.05);
        QVERIFY(std::abs(nodes[2].at.x - 800) < 5 && std::abs(nodes[2].at.y - 700) < 5); // the last node went round
        QVERIFY(nodes[2].in > 50); // its handles kept

        // Animated: the last node dragged on frame 4, a key of the curve.
        state.setRigMode(false);
        controller.setCurrentFrame(4);
        const QPointF last = *gizmo.partPoint(2, canvas::CurveGizmo::Part::Node);
        drag(last, last + QPointF(-60, 0));
        QVERIFY(controller.document().cel(curve)->motion().keyAt(doc::Channel::Deform, 4));
        const auto posed = deform::curveNodes(controller.document().cel(curve)->deformer(),
                                              controller.document().deformPoseAt(*controller.document().cel(curve), 4));
        QVERIFY(std::abs(posed[2].at.x - (nodes[2].at.x - 60)) < 3);
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
        QVERIFY(controller.celMotion(cel)->hasKeyAt(20));
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

    // Several keys chosen together take one curve to the next key at once, one undo step; a key not there is let be.
    void severalKeysTakeOneCurve()
    {
        document::DocumentController controller;
        controller.setSceneLength(48);
        const doc::CelId cel = controller.currentCel();
        QVERIFY(controller.setCelKey(cel, 0, at(0, 0)));
        QVERIFY(controller.setCelKey(cel, 12, at(100, 40)));
        QVERIFY(controller.setCelKey(cel, 24, at(200, 0)));
        using Ref = document::DocumentController::CelKeyRef;
        const std::vector<Ref> keys{{cel, 0, doc::Channel::Position}, {cel, 12, doc::Channel::Position},
                                    {cel, 12, doc::Channel::Rotation}, {cel, 30, doc::Channel::Position}};
        QVERIFY(controller.setCelKeysEasing(keys, doc::Easing::hold()));
        const doc::CelMotion *m = controller.celMotion(cel);
        QVERIFY(m->keyAt(doc::Channel::Position, 0)->easing == doc::Easing::hold());
        QVERIFY(m->keyAt(doc::Channel::Position, 12)->easing == doc::Easing::hold());
        QVERIFY(m->keyAt(doc::Channel::Rotation, 12)->easing == doc::Easing::hold());
        QVERIFY(!(m->keyAt(doc::Channel::Rotation, 0)->easing == doc::Easing::hold())); // not chosen
        QVERIFY(!(m->keyAt(doc::Channel::Position, 24)->easing == doc::Easing::hold()));
        QVERIFY(!controller.setCelKeysEasing(keys, doc::Easing::hold())); // nothing changes
        controller.undo();
        m = controller.celMotion(cel);
        QVERIFY(!(m->keyAt(doc::Channel::Position, 0)->easing == doc::Easing::hold()));
        QVERIFY(!(m->keyAt(doc::Channel::Position, 12)->easing == doc::Easing::hold()));
    }

    // Reversed, the keys play backwards over the frames they span: every frame shows what the mirrored frame showed, the
    // curves turned around too; the channels keep in step; one undo step on a trajectory.
    void aTrajectoryIsReversed()
    {
        doc::CelMotion motion;
        motion.pivotX = 5;
        motion.setKey(doc::Channel::Position, 4, at(0, 0), doc::Easing::easeIn());
        motion.setKey(doc::Channel::Position, 10, at(60, 30), doc::Easing::linear());
        motion.setKey(doc::Channel::Position, 20, at(100, -20));
        motion.setKey(doc::Channel::Opacity, 8, at(0, 0, 0, 1, 0.2));
        motion.setKey(doc::Channel::Opacity, 16, at(0, 0, 0, 1, 1.0));
        const doc::CelMotion back = motion.reversed();
        QCOMPARE(back.pivotX, 5.0);
        QCOMPARE(back.keyFrames(), (std::vector<int>{4, 8, 14, 16, 20}));
        for (int f = 0; f <= 26; ++f) { // also before and after the keys
            const int mirrored = std::clamp(24 - f, 0, 30);
            const doc::CelPose a = back.interpolatedAt(f), b = motion.interpolatedAt(mirrored);
            QVERIFY2(std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6 && std::abs(a.opacity - b.opacity) < 1e-6,
                     qPrintable(QStringLiteral("frame %1").arg(f)));
        }
        // Twice is as it was (the curves up to rounding: 1 − (1 − 0.42) is not quite 0.42 in doubles).
        const doc::CelMotion twice = back.reversed();
        for (doc::Channel c : doc::kChannels) {
            QCOMPARE(twice.keys(c).size(), motion.keys(c).size());
            for (std::size_t i = 0; i < motion.keys(c).size(); ++i) {
                const doc::CelKey &a = twice.keys(c)[i], &b = motion.keys(c)[i];
                QCOMPARE(a.frame, b.frame);
                QCOMPARE(a.pose, b.pose);
                QCOMPARE(a.easing.kind, b.easing.kind);
                QVERIFY(std::abs(a.easing.x1 - b.easing.x1) < 1e-12 && std::abs(a.easing.y1 - b.easing.y1) < 1e-12
                        && std::abs(a.easing.x2 - b.easing.x2) < 1e-12 && std::abs(a.easing.y2 - b.easing.y2) < 1e-12);
            }
        }
        const doc::Easing out = back.keyAt(doc::Channel::Position, 14)->easing; // the ease in, turned around
        const doc::Easing easeOut = doc::Easing::easeOut();
        QVERIFY(out.kind == easeOut.kind && std::abs(out.x1 - easeOut.x1) < 1e-12 && std::abs(out.y1 - easeOut.y1) < 1e-12
                && std::abs(out.x2 - easeOut.x2) < 1e-12 && std::abs(out.y2 - easeOut.y2) < 1e-12);

        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId path = controller.currentCel();
        QVERIFY(controller.setCelKey(path, 2, at(0, 0)));
        QVERIFY(!controller.reverseCelMotion(path)); // one key: nothing to turn around
        QVERIFY(controller.setCelKey(path, 12, at(100, 50)));
        QVERIFY(controller.reverseCelMotion(path));
        QCOMPARE(controller.celPoseAt(path, 2), at(100, 50));
        QCOMPARE(controller.celPoseAt(path, 12), at(0, 0));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        controller.undo();
        QCOMPARE(controller.celPoseAt(path, 2), at(0, 0));
    }

    // A cel that follows a trajectory is drawn where the trajectory takes it, on top of its own keys; and only a trajectory
    // can be followed. One undo step; a trajectory that goes lets the cel go.
    void aCelFollowsATrajectory()
    {
        document::DocumentController controller;
        controller.setSceneLength(30);
        const doc::Document &d = controller.document();
        const doc::CelId piece = d.celAt(0).id();
        controller.addTrajectory();
        const doc::CelId path = controller.currentCel();
        controller.addCel();
        const doc::CelId plain = controller.currentCel();
        QVERIFY(!controller.setTrajectoryParent(piece, plain)); // not a trajectory
        QVERIFY(controller.setTrajectoryParent(piece, path));
        QVERIFY(!controller.setTrajectoryParent(piece, path)); // nothing changes
        QCOMPARE(d.followedTrajectory(*d.cel(piece))->id(), path);
        QVERIFY(d.followMatrix(*d.cel(piece), 0).isIdentity()); // the trajectory does nothing yet
        QVERIFY(d.trajectoryParent(*d.cel(piece)) == nullptr); // (it is not a trajectory's child)

        // Relative to the trajectory's first key: there (and before) the cel stays, then it goes as far as the trajectory did.
        doc::CelPose slide;
        slide.x = 100;
        controller.setCelKey(path, 5, doc::CelPose{});
        controller.setCelKey(path, 15, slide);
        double x = 0, y = 0;
        d.followMatrix(*d.cel(piece), 5).map(10, 20, x, y);
        QCOMPARE(QPointF(x, y), QPointF(10, 20));
        d.followMatrix(*d.cel(piece), 0).map(10, 20, x, y);
        QCOMPARE(QPointF(x, y), QPointF(10, 20));
        d.followMatrix(*d.cel(piece), 15).map(10, 20, x, y);
        QCOMPARE(QPointF(x, y), QPointF(110, 20));
        QCOMPARE(d.trajectoryStartFrame(path), 5);
        // Drawn there.
        doc::Document document(200, 120);
        document.setFrameCount(30);
        document.setPaperColor(0xffffffff);
        const doc::CelId square = addSquare(document, 40, 40, 20, 0);
        doc::CelProperties line{"Path"};
        line.trajectory = true;
        doc::Cel &trajectory = document.insertCel(1, std::make_unique<doc::Cel>(document.allocateCelId(), line));
        doc::CelMotion motion;
        motion.setKey(doc::Channel::Position, 0, doc::CelPose{0, 0});
        motion.setKey(doc::Channel::Position, 10, doc::CelPose{100, 0});
        trajectory.setMotion(motion);
        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(50, 50), QColor(Qt::red)); // not following yet
        doc::CelProperties follower = document.cel(square)->properties();
        follower.parent = trajectory.id();
        document.cel(square)->setProperties(follower);
        renderer.renderAll(document, 0);
        QCOMPARE(renderer.image().pixelColor(50, 50), QColor(Qt::red)); // at the first key: where it is drawn
        renderer.renderAll(document, 10);
        QCOMPARE(renderer.image().pixelColor(150, 50), QColor(Qt::red));
        QCOMPARE(renderer.image().pixelColor(50, 50), QColor(Qt::white));
        controller.undo(); // the trajectory's two keys
        controller.undo();
        controller.undo(); // the link
        QVERIFY(d.followedTrajectory(*d.cel(piece)) == nullptr);
        controller.redo();
        controller.redo();
        controller.redo();
        QVERIFY(controller.setTrajectoryParent(piece, doc::kNoId)); // let go
        QVERIFY(d.followedTrajectory(*d.cel(piece)) == nullptr);
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
        keyAll(motion, 0, at(0, 0), doc::Easing::linear());
        keyAll(motion, 10, at(100, 0), doc::Easing::linear());
        keyAll(motion, 20, at(100, 0, 0, 1, 0.5), doc::Easing::linear());
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
        keyAll(motion, 0, at(0, 0, 90), doc::Easing::hold()); // a quarter turn clockwise
        document.cel(piece)->setMotion(motion);
        canvas::FrameRenderer renderer;
        renderer.renderAll(document, 0);
        // The square's right part (x 100..119 at y 100) goes down from the pivot: x 90..110, y 100..119.
        QCOMPARE(renderer.image().pixelColor(100, 115), QColor(Qt::red));
        QCOMPARE(renderer.image().pixelColor(115, 100), QColor(Qt::white));
        // Scaled by 2 around the pivot (a key held on).
        keyAll(motion, 0, at(0, 0, 0, 2));
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
        keyAll(motion, 0, at(40.5, 0.5, 0, 3), doc::Easing::hold()); // off the pixel grid, three times as big
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
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1));

        // On the key, inside the box: the key moves by as much as the pen moved.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(960, 540));
        QTest::mouseMove(&canvas, at(1000, 560));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1000, 560));
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1));
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
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1)); // not a key
        QVERIFY(controller.celPoseAt(cel, 10).x > 120);
        QCOMPARE(canvas.documentImage().pixelColor(1100, 560), QColor(Qt::red));
        const double temporary = controller.celPoseAt(cel, 10).x;
        controller.setCurrentFrame(0);
        controller.setCurrentFrame(10);
        QCOMPARE(controller.celPoseAt(cel, 10).x, moved.x); // all as before: it went when the frame was left
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1));

        // The Cel key button before leaving makes it a key.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(1000, 560));
        QTest::mouseMove(&canvas, at(1100, 560));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1100, 560));
        controller.toggleCelKey(cel);
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(2));
        QCOMPARE(controller.celPoseAt(cel, 10).x, temporary);
        controller.setCurrentFrame(0);
        controller.setCurrentFrame(10);
        QCOMPARE(controller.celPoseAt(cel, 10).x, temporary); // it stayed
        const double midway = controller.celPoseAt(cel, 5).x;
        QVERIFY(midway > controller.celPoseAt(cel, 0).x && midway < temporary);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        controller.undo(); // the key made
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1));
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

        // A corner scales it from the opposite corner, as the Transform tool of the pixels does: twice as far out, twice as
        // big, and that corner stays where it was. Shift keeps the proportions; a side scales one way only.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(990, 560));
        QTest::mouseMove(&canvas, at(930 + 2 * 60, 520 + 2 * 40));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(930 + 2 * 60, 520 + 2 * 40));
        QVERIFY2(std::abs(controller.celPoseAt(cel, 0).scaleX - 2.0) < 0.1, qPrintable(QString::number(controller.celPoseAt(cel, 0).scaleX)));
        QVERIFY(std::abs(controller.celPoseAt(cel, 0).scaleY - 2.0) < 0.1);
        QCOMPARE(canvas.documentImage().pixelColor(935, 525), QColor(Qt::red));  // the top left corner stayed
        QCOMPARE(canvas.documentImage().pixelColor(1040, 580), QColor(Qt::red)); // and the piece grew from it
        controller.undo();
        // The middle of the right side: only the width.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(990, 540));
        QTest::mouseMove(&canvas, at(930 + 120, 540));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(930 + 120, 540));
        QVERIFY(std::abs(controller.celPoseAt(cel, 0).scaleX - 2.0) < 0.1);
        QCOMPARE(controller.celPoseAt(cel, 0).scaleY, 1.0);
        controller.undo();

        // Outside the box nothing happens (but a turn), and with a drawing tool the cel is shown as ever: moved by its keys.
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, at(100, 100));
        QCOMPARE(controller.celPoseAt(cel, 0), doc::CelPose{});
        controller.setCelKey(cel, 0, [] { doc::CelPose p; p.x = 300; return p; }());
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::red)); // posed (the tool moves the cel)
        state.setActiveTool(tools::ToolId::Brush);
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::red)); // moved, to draw on it as it is shown
        QCOMPARE(canvas.documentImage().pixelColor(960, 540), QColor(Qt::white));
        state.setActiveTool(tools::ToolId::Transform);
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::red));
        controller.removeCelKey(cel, 0); // no keys: the same tool is on the pixels, the cel is as drawn
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::white));
    }

    // Under the "Keyframes" row, a row for each channel that has keys: a drag on a diamond there moves that channel's key
    // alone, a double click on an empty cell makes a key of that channel alone.
    void aCelShowsTheKeysOfItsDeformerAndTrajectoryInside()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(40);
        const doc::CelId cel = controller.currentCel();
        controller.addTrajectory();
        const doc::CelId path = controller.currentCel();
        controller.setCelKey(path, 2, controller.celPoseAt(path, 2));
        controller.chooseCel(cel);
        controller.addDeformer(doc::DeformerKind::Bones); // the cel is bent by it; it follows the trajectory the cel did
        const doc::CelId bones = controller.currentCel();
        controller.setTrajectoryParent(bones, path);
        controller.addBone(bones, -1, {100, 100}, {200, 100});
        controller.setDeformKey(bones, 4, controller.document().cel(bones)->deformer().rest);
        controller.chooseCel(cel);
        ui::TimelinePanel panel(&controller, &state);
        panel.resize(900, 360);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        ui::TimelineView *view = panel.view();
        // Rows of the cel: itself, then its deformer, then the trajectory the deformer follows.
        int row = -1;
        for (int r = 0; r < view->rowCount(); ++r)
            if (!view->isMotionRow(r) && controller.document().celAt(view->celIndexForRow(r)).id() == cel)
                row = r;
        QVERIFY(row >= 0);
        QVERIFY(view->isLinkRow(row + 1) && view->isLinkRow(row + 2));
        QCOMPARE(controller.document().celAt(view->celIndexForRow(row + 1)).id(), bones);
        QCOMPARE(controller.document().celAt(view->celIndexForRow(row + 2)).id(), path);
        // A key dragged on the deformer's row inside the cel: the deformer's own key moves.
        ui::TimelineMetrics m;
        auto center = [&](int r, int frame) { return QPoint(m.frameX(frame) + m.frameWidth / 2, m.rowY(r) + m.rowHeight / 2); };
        auto *vp = view->viewport();
        QTest::mousePress(vp, Qt::LeftButton, {}, center(row + 1, 4));
        QTest::mouseMove(vp, center(row + 1, 6));
        QTest::mouseMove(vp, center(row + 1, 8));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, center(row + 1, 8));
        const doc::CelMotion &motion = controller.document().cel(bones)->motion();
        QVERIFY(motion.keyAt(doc::Channel::Deform, 8) && !motion.keyAt(doc::Channel::Deform, 4));
        // And the trajectory's, on its row.
        QTest::mousePress(vp, Qt::LeftButton, {}, center(row + 2, 2));
        QTest::mouseMove(vp, center(row + 2, 4));
        QTest::mouseMove(vp, center(row + 2, 5));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, center(row + 2, 5));
        QVERIFY(controller.celMotion(path)->hasKeyAt(5) && !controller.celMotion(path)->hasKeyAt(2));
        // A cel carried by a bone shows its deformer too.
        QVERIFY(controller.setDeformerParent(cel, bones, 0));
        QVERIFY(view->isLinkRow(row + 1));
        QCOMPARE(controller.document().celAt(view->celIndexForRow(row + 1)).id(), bones);
    }

    void theTimelineHasARowForEachChannel()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(40);
        const doc::CelId cel = controller.currentCel();
        ui::TimelinePanel panel(&controller, &state);
        panel.resize(900, 320);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        controller.setCurrentFrame(4);
        controller.toggleCelKey(cel); // position, rotation and scale: three rows under "Keyframes"
        ui::TimelineMetrics m;
        auto *vp = panel.view()->viewport();
        auto center = [&](int row, int frame) { return QPoint(m.frameX(frame) + m.frameWidth / 2, m.rowY(row) + m.rowHeight / 2); };
        // The channels' rows are closed to begin with: the arrow of "Keyframes" opens them.
        QVERIFY(!panel.view()->channelRowsShown(0));
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(m.headerWidth - 12, m.rowY(1) + m.rowHeight / 2));
        QVERIFY(panel.view()->channelRowsShown(0));
        // Rows: 0 the cel, 1 Keyframes, 2 Position, 3 Rotation, 4 Scale.
        QTest::mousePress(vp, Qt::LeftButton, {}, center(3, 4));
        QTest::mouseMove(vp, center(3, 7));
        QTest::mouseMove(vp, center(3, 9));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, center(3, 9));
        const doc::CelMotion *motion = controller.celMotion(cel);
        QVERIFY(motion->keyAt(doc::Channel::Rotation, 9) && !motion->keyAt(doc::Channel::Rotation, 4));
        QVERIFY(motion->keyAt(doc::Channel::Position, 4) && motion->keyAt(doc::Channel::Scale, 4)); // the others stay
        // A click on a diamond of a channel's row chooses that key alone; on the "Keyframes" row, the keys of the frame.
        QTest::mouseClick(vp, Qt::LeftButton, {}, center(3, 9));
        QVERIFY(panel.view()->selectedKey() && panel.view()->selectedKey()->second == 9);
        QCOMPARE(panel.view()->selectedKeyChannel(), std::optional<doc::Channel>(doc::Channel::Rotation));
        QTest::mouseClick(vp, Qt::LeftButton, {}, center(2, 4));
        QCOMPARE(panel.view()->selectedKey()->second, 4);
        QCOMPARE(panel.view()->selectedKeyChannel(), std::optional<doc::Channel>(doc::Channel::Position));
        QTest::mouseClick(vp, Qt::LeftButton, {}, center(1, 4));
        QVERIFY(!panel.view()->selectedKeyChannel());
        // Delete takes away the key that is chosen: the position's alone, one undo step; and then nothing is chosen.
        QTest::mouseClick(vp, Qt::LeftButton, {}, center(2, 4));
        QTest::keyClick(panel.view(), Qt::Key_Delete);
        QVERIFY(!motion->keyAt(doc::Channel::Position, 4));
        QVERIFY(motion->keyAt(doc::Channel::Scale, 4) && motion->keyAt(doc::Channel::Rotation, 9));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        controller.undo();
        QVERIFY(motion->keyAt(doc::Channel::Position, 4));
        QTest::keyClick(panel.view(), Qt::Key_Delete); // nothing chosen now: nothing happens
        QVERIFY(motion->keyAt(doc::Channel::Position, 4));
        // A double click on the scale's row: a key of the scale alone.
        QMouseEvent press(QEvent::MouseButtonDblClick, QPointF(center(4, 20)), vp->mapToGlobal(QPointF(center(4, 20))),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(center(4, 20)), vp->mapToGlobal(QPointF(center(4, 20))),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &release);
        QVERIFY(motion->keyAt(doc::Channel::Scale, 20));
        QVERIFY(!motion->keyAt(doc::Channel::Position, 20) && !motion->keyAt(doc::Channel::Rotation, 20));
        // On the "Keyframes" row a drag moves what is on that frame, all of its channels.
        QTest::mousePress(vp, Qt::LeftButton, {}, center(1, 4));
        QTest::mouseMove(vp, center(1, 6));
        QTest::mouseMove(vp, center(1, 12));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, center(1, 12));
        QVERIFY(motion->keyAt(doc::Channel::Position, 12) && motion->keyAt(doc::Channel::Scale, 12));
        QVERIFY(!motion->keyAt(doc::Channel::Position, 4) && !motion->keyAt(doc::Channel::Scale, 4));
    }

    // Keys of the "Keyframes" row chosen with Ctrl (one more, or one less) and Shift (a run of the row), moved together,
    // deleted together, copied and pasted from the current frame (onto the current cel), each one undo step.
    void keysAreChosenCopiedAndPasted()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(60);
        const doc::CelId cel = controller.currentCel();
        ui::TimelinePanel panel(&controller, &state);
        panel.resize(900, 320);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        for (int frame : {2, 5, 8, 20}) {
            controller.setCurrentFrame(frame);
            controller.setCelKey(cel, frame, [frame] { doc::CelPose p; p.x = frame * 10; return p; }());
        }
        ui::TimelineMetrics m;
        auto *vp = panel.view()->viewport();
        auto at = [&](int frame) { return QPoint(m.frameX(frame) + m.frameWidth / 2, m.rowY(1) + m.rowHeight / 2); }; // "Keyframes"
        const doc::CelMotion *motion = controller.celMotion(cel);
        auto has = [&](int frame) { return motion->hasKeyAt(frame); };
        // Shift: from the last clicked to this one; Ctrl: one less.
        QTest::mouseClick(vp, Qt::LeftButton, {}, at(2));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ShiftModifier, at(8));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, at(5));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, at(20));
        // Chosen now: 2, 8 and 20. They go together by a drag of one of them, one undo step.
        QTest::mousePress(vp, Qt::LeftButton, {}, at(8));
        QTest::mouseMove(vp, at(10));
        QTest::mouseMove(vp, at(12));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, at(12));
        QVERIFY(has(6) && has(5) && has(12) && has(24) && !has(2) && !has(8) && !has(20));
        controller.undo();
        QVERIFY(has(2) && has(5) && has(8) && has(20) && !has(6));
        controller.redo();
        // A key in the way is a place taken: the whole move is refused.
        QTest::mousePress(vp, Qt::LeftButton, {}, at(6));
        QTest::mouseMove(vp, at(7));
        QTest::mouseMove(vp, at(5)); // 6 -> 5: taken by a key that stays (the key on 5 is not chosen)
        QTest::mouseRelease(vp, Qt::LeftButton, {}, at(5));
        QVERIFY(has(6) && has(5) && has(12) && has(24));
        // Copy the keys 5 and 6 (Shift from 5 to 6), paste them from frame 30.
        QTest::mouseClick(vp, Qt::LeftButton, {}, at(5));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ShiftModifier, at(6));
        QTest::keyClick(panel.view(), Qt::Key_C, Qt::ControlModifier);
        controller.setCurrentFrame(30);
        QTest::keyClick(panel.view(), Qt::Key_V, Qt::ControlModifier);
        QVERIFY(has(30) && has(31));
        QCOMPARE(motion->poseAt(30).x, 50.0);
        QCOMPARE(motion->poseAt(31).x, 20.0); // the key that came from frame 2
        controller.undo(); // both, one step
        QVERIFY(!has(30) && !has(31));
        controller.redo();
        // Pasted on a key: it is replaced.
        controller.setCurrentFrame(12);
        QTest::keyClick(panel.view(), Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(motion->poseAt(12).x, 50.0);
        QVERIFY(has(13));
        // The pasted ones are chosen: Delete takes both.
        QTest::keyClick(panel.view(), Qt::Key_Delete);
        QVERIFY(!has(12) && !has(13));
    }

    // A cel moved by its keys is shown moved at rest too, whatever the tool, and the pen draws on it as it is shown: what is
    // drawn at the pen goes into the cel's pixels at their own place, and shows under the pen.
    void drawingOnAPosedCelPaintsWhereThePenIs()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece")); // x 930..989, y 520..559
        const doc::CelId cel = controller.currentCel();
        controller.setCelKey(cel, 0, [] { doc::CelPose p; p.x = 300; return p; }()); // shown at x 1230..1289
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush);
        state.setColor(QColor(0, 0, 255));
        QCOMPARE(canvas.documentImage().pixelColor(1240, 540), QColor(Qt::red)); // shown moved, with the pen tool
        const QTransform toView = canvas.view().documentToView();
        auto at = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(1240, 540));
        for (int x = 1241; x <= 1270; x += 3)
            QTest::mouseMove(&canvas, at(x, 540));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1270, 540));
        // In the cel's own pixels the stroke is where the cel is drawn (the pose taken off), not under the pen.
        const doc::TiledImage &pixels = controller.document().cel(cel)->drawingAt(0)->layers.front().image;
        QVERIFY(qBlue(pixels.pixel(950, 540)) > 200 && qRed(pixels.pixel(950, 540)) < 80);
        QCOMPARE(qAlpha(pixels.pixel(1250, 540)), 0);
        // And on the canvas it shows under the pen.
        const QColor shown = canvas.documentImage().pixelColor(1250, 540);
        QVERIFY(shown.blue() > 200 && shown.red() < 80);
        QCOMPARE(canvas.documentImage().pixelColor(960, 540), QColor(Qt::white)); // nothing where the cel was drawn

        // A turned cel: a quarter turn round the middle (960, 540) shows the pixel at (980, 525) at (975, 560).
        controller.setCelKey(cel, 0, [] { doc::CelPose p; p.rotation = 90; return p; }());
        QCOMPARE(qRed(pixels.pixel(980, 525)), 255);
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, at(975, 560));
        QVERIFY(qBlue(pixels.pixel(980, 525)) > 200 && qRed(pixels.pixel(980, 525)) < 80);
    }

    // A trajectory cel: made from the arrow next to the new cel button's plus (here, directly), it has no drawings and is not drawn;
    // selected, it shows a circle (its middle moves it, its arrow turns it, its edge scales it) whatever the tool, and what is
    // dragged becomes its keys, in one undo step; nothing can be drawn on it.
    void bonesAreDrawnInRigModeAndTurnedToAnimate()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        const doc::CelId drawn = controller.currentCel();
        controller.addDeformer(doc::DeformerKind::Bones);
        const doc::CelId bones = controller.currentCel();
        state.setRigMode(true);
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush); // whatever the tool
        const canvas::BoneGizmo &gizmo = canvas.boneGizmo();
        QVERIFY(gizmo.editable());
        const QTransform toView = canvas.view().documentToView();
        auto at = [&](const QPointF &docPoint) { return toView.map(docPoint).toPoint(); };
        auto drag = [&](QPointF from, QPointF to) {
            QTest::mousePress(&canvas, Qt::LeftButton, {}, at(from));
            for (int k = 1; k <= 4; ++k)
                QTest::mouseMove(&canvas, at(from + (to - from) * k / 4.0));
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(to));
        };
        const doc::Deformer &rig = controller.document().cel(bones)->deformer();

        // On nothing: a root; from its end: the next one, hanging from it.
        drag({600, 500}, {900, 500});
        QCOMPARE(rig.bones.size(), std::size_t(1));
        QVERIFY(std::abs(rig.rest[doc::BoneLength] - 300) < 4);
        drag({900, 500}, {1200, 500});
        QCOMPARE(rig.bones.size(), std::size_t(2));
        QCOMPARE(rig.bones[1].parent, 0);
        QCOMPARE(gizmo.chosenBone(), 1);
        // Delete takes the chosen one away; undo brings it back.
        QTest::keyClick(&canvas, Qt::Key_Delete);
        QCOMPARE(rig.bones.size(), std::size_t(1));
        controller.undo();
        QCOMPARE(controller.document().cel(bones)->deformer().bones.size(), std::size_t(2));
        // The joint between them dragged: both bones meet where it went, their other ends stay.
        drag({900, 500}, {900, 400});
        const auto frames = deform::boneFrames(controller.document().cel(bones)->deformer(),
                                               controller.document().cel(bones)->deformer().rest);
        QVERIFY(std::abs(frames[0].end().y - 400) < 3 && std::abs(frames[1].origin.y - 400) < 3);
        QVERIFY(std::abs(frames[1].end().x - 1200) < 3 && std::abs(frames[1].end().y - 500) < 3);
        QVERIFY(std::abs(frames[0].origin.x - 600) < 3);

        // Animated: the forearm turned on frame 5, the first key of the deformer, one undo step.
        state.setRigMode(false);
        controller.setCurrentFrame(5);
        const QPointF elbow = *gizmo.partPoint(1, canvas::BoneGizmo::Part::Origin);
        const QPointF hand = *gizmo.partPoint(1, canvas::BoneGizmo::Part::End);
        const double reach = QLineF(elbow, hand).length();
        const double from = std::atan2(hand.y() - elbow.y(), hand.x() - elbow.x());
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(hand));
        for (int k = 1; k <= 6; ++k) {
            const double a = from + k * (M_PI / 4) / 6;
            QTest::mouseMove(&canvas, at(elbow + QPointF(std::cos(a), std::sin(a)) * reach));
        }
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(elbow + QPointF(std::cos(from + M_PI / 4), std::sin(from + M_PI / 4)) * reach));
        const doc::Cel *deformer = controller.document().cel(bones);
        QVERIFY(deformer->motion().keyAt(doc::Channel::Deform, 5));
        const std::vector<double> pose = controller.document().deformPoseAt(*deformer, 5);
        QVERIFY(std::abs(pose[4 + doc::BoneAngle] - (deformer->deformer().rest[4 + doc::BoneAngle] + 45)) < 1.5);
        QCOMPARE(pose[doc::BoneAngle], deformer->deformer().rest[doc::BoneAngle]); // the upper arm stays
        controller.undo();
        QVERIFY(!controller.document().cel(bones)->motion().hasKeys(doc::Channel::Deform));
        QCOMPARE(controller.document().cel(drawn)->properties().parent, bones);
    }

    void aTrajectoryCelIsMovedByItsCircle()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        const int cels = controller.document().celCount();
        controller.addTrajectory();
        QCOMPARE(controller.document().celCount(), cels + 1);
        const doc::CelId id = controller.currentCel();
        const doc::Cel *cel = controller.document().cel(id);
        QVERIFY(cel->properties().trajectory);
        QVERIFY(cel->drawings().empty());
        QVERIFY(cel->motion().isEmpty());
        QCOMPARE(cel->motion().pivotX, controller.document().width() / 2.0); // its circle is at the middle of the page
        controller.undo(); // one step: made, with its pivot
        QCOMPARE(controller.document().celCount(), cels);
        controller.redo();
        controller.chooseCel(id); // (a redo does not choose it again)

        QString why;
        QVERIFY(!controller.beginPaint(true, &why)); // nothing is drawn on it
        QVERIFY(!why.isEmpty());

        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush); // whatever the tool
        const canvas::TrajectoryGizmo &gizmo = canvas.trajectoryGizmo();
        QVERIFY(gizmo.editable());
        const canvas::ViewTransform &view = canvas.view();
        const QTransform toView = view.documentToView();
        auto at = [&](const QPointF &docPoint) { return toView.map(docPoint).toPoint(); };
        const auto middle = gizmo.handlePoint(canvas::TrajectoryGizmo::Handle::Move, view);
        QVERIFY(middle && qAbs(middle->x() - 960) < 1 && qAbs(middle->y() - 540) < 1);
        QCOMPARE(gizmo.handleAt(*middle, view), canvas::TrajectoryGizmo::Handle::Move);
        QCOMPARE(gizmo.handleAt(QPointF(100, 100), view), canvas::TrajectoryGizmo::Handle::None);

        // The middle moved: the first key, at the pose the drag ends in, as one step.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(*middle));
        QTest::mouseMove(&canvas, at(*middle + QPointF(40, 10)));
        QTest::mouseMove(&canvas, at(*middle + QPointF(80, 30)));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(*middle + QPointF(80, 30)));
        QVERIFY(controller.celMotion(id)->hasKeyAt(0));
        QVERIFY(qAbs(controller.celPoseAt(id, 0).x - 80) < 1.5 && qAbs(controller.celPoseAt(id, 0).y - 30) < 1.5);
        controller.undo(); // the first key and the drag are one step
        QVERIFY(controller.celMotion(id)->isEmpty());
        controller.redo();
        QVERIFY(controller.celMotion(id)->hasKeyAt(0));
        const auto moved = gizmo.handlePoint(canvas::TrajectoryGizmo::Handle::Move, view);
        QVERIFY(moved && qAbs(moved->x() - 1040) < 1.5); // the circle followed

        // The arrow turned a quarter: its tip goes from the right of the circle to below it (clockwise).
        const auto tip = gizmo.handlePoint(canvas::TrajectoryGizmo::Handle::Rotate, view);
        QVERIFY(tip && tip->x() > moved->x());
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(*tip));
        for (int a = 15; a <= 90; a += 15) {
            const double r = std::hypot(tip->x() - moved->x(), tip->y() - moved->y());
            QTest::mouseMove(&canvas, at(*moved + QPointF(r * std::cos(a * M_PI / 180), r * std::sin(a * M_PI / 180))));
        }
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(*moved + QPointF(0, std::hypot(tip->x() - moved->x(), tip->y() - moved->y()))));
        QVERIFY2(qAbs(controller.celPoseAt(id, 0).rotation - 90) < 3, qPrintable(QString::number(controller.celPoseAt(id, 0).rotation)));

        // The edge dragged out to twice the radius doubles the scale (and the circle).
        const auto edge = gizmo.handlePoint(canvas::TrajectoryGizmo::Handle::Scale, view);
        QVERIFY(edge);
        const QPointF center = *gizmo.handlePoint(canvas::TrajectoryGizmo::Handle::Move, view);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(*edge));
        QTest::mouseMove(&canvas, at(center + (*edge - center) * 1.5));
        QTest::mouseMove(&canvas, at(center + (*edge - center) * 2.0));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(center + (*edge - center) * 2.0));
        QVERIFY2(qAbs(controller.celPoseAt(id, 0).scaleX - 2.0) < 0.1, qPrintable(QString::number(controller.celPoseAt(id, 0).scaleX)));
        controller.undo(); // each drag one step
        QVERIFY(qAbs(controller.celPoseAt(id, 0).scaleX - 1.0) < 0.01);
    }

    // In trajectory mode, with a path shown, Shift is for the path's keys: with a paint tool it does not ready the straight
    // line from the last point (nothing is drawn then); out of trajectory mode it does again.
    void shiftChoosesPathKeysNotAStraightLineInTrajectoryMode()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        QVERIFY(controller.setCelKey(trajectory, 2, at(0, 0)));
        QVERIFY(controller.setCelKey(trajectory, 12, at(200, 100)));
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece"));
        const doc::CelId follower = controller.currentCel();
        QVERIFY(controller.setTrajectoryParent(follower, trajectory));

        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush);
        const QTransform toView = canvas.view().documentToView();
        auto screen = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        const double px = controller.document().cel(trajectory)->motion().pivotX;
        const double py = controller.document().cel(trajectory)->motion().pivotY;
        controller.setLayerChosen(true); // its layer, to draw on
        { // a short stroke, away from the path: there is a last point
            QTest::mousePress(&canvas, Qt::LeftButton, {}, screen(px - 300, py + 250));
            QTest::mouseMove(&canvas, screen(px - 290, py + 250));
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, screen(px - 280, py + 250));
        }

        state.setTrajectoryMode(true);
        QVERIFY(canvas.trajectoryGizmo().pathShown());
        QTest::keyPress(&canvas, Qt::Key_Shift);
        QVERIFY(!canvas.straightLineArmed());
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, screen(px, py));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ShiftModifier, screen(px + 200, py + 100));
        QCOMPARE(canvas.trajectoryGizmo().chosenKeys(), (std::set<int>{2, 12}));
        QVERIFY(!canvas.straightLineArmed());
        QTest::keyRelease(&canvas, Qt::Key_Shift);

        state.setTrajectoryMode(false);
        QTest::keyPress(&canvas, Qt::Key_Shift);
        QVERIFY(canvas.straightLineArmed());
        state.setTrajectoryMode(true); // turned on while held: the line goes
        QVERIFY(!canvas.straightLineArmed());
        QTest::keyRelease(&canvas, Qt::Key_Shift);
    }

    // While the path is shown the selection tools choose its keys (a box, a lasso; Shift adds, Alt takes away), and a
    // chosen key dragged takes the others with it, as far on the page, in one undo step.
    void pathKeysAreChosenAndMovedTogether()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        QVERIFY(controller.setCelKey(trajectory, 2, at(0, 0)));
        QVERIFY(controller.setCelKey(trajectory, 12, at(200, 100)));
        QVERIFY(controller.setCelKey(trajectory, 22, at(400, 0)));
        controller.addCel();
        const doc::CelId follower = controller.currentCel();
        QVERIFY(controller.setTrajectoryParent(follower, trajectory));

        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setTrajectoryMode(true);
        const canvas::TrajectoryGizmo &gizmo = canvas.trajectoryGizmo();
        const QTransform toView = canvas.view().documentToView();
        auto screen = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        const double px = controller.document().cel(trajectory)->motion().pivotX;
        const double py = controller.document().cel(trajectory)->motion().pivotY;
        auto drag = [&](QPoint from, std::vector<QPoint> through, Qt::KeyboardModifiers modifiers = {}) {
            QTest::mousePress(&canvas, Qt::LeftButton, modifiers, from);
            for (const QPoint &p : through)
                QTest::mouseMove(&canvas, p);
            QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, through.empty() ? from : through.back());
        };

        // A box round the keys of frames 12 and 22.
        state.setActiveTool(tools::ToolId::RectSelect);
        drag(screen(px + 140, py - 60), {screen(px + 300, py + 60), screen(px + 440, py + 140)});
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12, 22}));
        QVERIFY(!controller.hasSelection()); // no pixels selected

        // The key of 12 dragged: 22 goes with it, 2 stays; one undo step.
        drag(screen(px + 200, py + 100), {screen(px + 230, py + 120), screen(px + 260, py + 140)});
        auto near = [&](int frame, double x, double y) {
            const doc::CelPose p = controller.celPoseAt(trajectory, frame);
            return qAbs(p.x - x) < 3 && qAbs(p.y - y) < 3;
        };
        QVERIFY(near(12, 260, 140));
        QVERIFY(near(22, 460, 40));
        QVERIFY(near(2, 0, 0));
        controller.undo();
        QVERIFY(near(12, 200, 100) && near(22, 400, 0));

        // A lasso with Alt takes 22 away; Shift and a click on 2 adds it.
        state.setActiveTool(tools::ToolId::LassoSelect);
        drag(screen(px + 360, py - 40), {screen(px + 440, py - 40), screen(px + 440, py + 40), screen(px + 360, py + 40)},
             Qt::AltModifier);
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12}));
        drag(screen(px, py), {}, Qt::ShiftModifier);
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{2, 12}));
        // A key clicked alone is chosen alone; a click on nothing chooses none.
        drag(screen(px + 400, py), {});
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{22}));
        drag(screen(px + 100, py + 300), {});
        QVERIFY(gizmo.chosenKeys().empty());

        // As on the timeline: Ctrl+click adds a key or takes it away (nothing moved), Shift+click adds those from the one
        // last clicked.
        drag(screen(px, py), {});
        drag(screen(px + 400, py), {}, Qt::ControlModifier);
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{2, 22}));
        drag(screen(px, py), {}, Qt::ControlModifier);
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{22}));
        QVERIFY(near(2, 0, 0) && near(22, 400, 0));
        drag(screen(px, py), {});
        drag(screen(px + 400, py), {}, Qt::ShiftModifier);
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{2, 12, 22}));
    }

    // The menu of a cel's parent trajectory: a trajectory gets "Parent to" (the other trajectories, the parent checked,
    // None to let go; one that follows it cannot be its parent), a cel that is no trajectory "Follow trajectory".
    void aTrajectoryIsParentedToAnotherFromItsMenu()
    {
        document::DocumentController controller;
        controller.addTrajectory();
        const doc::CelId a = controller.currentCel();
        controller.addTrajectory();
        const doc::CelId b = controller.currentCel();
        controller.addTrajectory();
        const doc::CelId c = controller.currentCel();
        QVERIFY(controller.setTrajectoryParent(b, a)); // b follows a
        const doc::Document &d = controller.document();
        auto name = [&](doc::CelId id) { return QString::fromStdString(d.cel(id)->properties().name); };
        QVERIFY(name(a) != name(b) && name(b) != name(c) && name(a) != name(c));

        QMenu menu;
        ui::addParentTrajectoryMenu(menu, &controller, *d.cel(a), &menu);
        QMenu *parents = menu.actions().value(0) ? menu.actions().value(0)->menu() : nullptr;
        QVERIFY(parents);
        QCOMPARE(parents->title(), QStringLiteral("Parent to"));
        auto action = [&](const QString &text) -> QAction * {
            for (QAction *x : parents->actions())
                if (x->text() == text)
                    return x;
            return nullptr;
        };
        QVERIFY(action(QStringLiteral("None")) && action(QStringLiteral("None"))->isChecked());
        QVERIFY(!action(name(a))); // not itself
        QVERIFY(action(name(b)) && !action(name(b))->isEnabled()); // b follows a: a loop
        QVERIFY(action(name(c)) && action(name(c))->isEnabled());
        action(name(c))->trigger();
        QCOMPARE(d.trajectoryParent(*d.cel(a))->id(), c);
        controller.undo(); // one step
        QVERIFY(!d.trajectoryParent(*d.cel(a)));
        controller.redo();

        // Back: c is the parent, checked; None lets it go.
        QMenu again;
        ui::addParentTrajectoryMenu(again, &controller, *d.cel(a), &again);
        parents = again.actions().value(0)->menu();
        QVERIFY(action(name(c))->isChecked());
        QVERIFY(!action(QStringLiteral("None"))->isChecked());
        action(QStringLiteral("None"))->trigger();
        QVERIFY(!d.trajectoryParent(*d.cel(a)));

        // A cel that is no trajectory: Follow trajectory, all three to choose from.
        controller.addCel();
        QMenu cel;
        ui::addParentTrajectoryMenu(cel, &controller, *d.cel(controller.currentCel()), &cel);
        parents = cel.actions().value(0)->menu();
        QCOMPARE(parents->title(), QStringLiteral("Follow trajectory"));
        QVERIFY(action(name(a)) && action(name(b)) && action(name(c)));
        QVERIFY(action(name(a))->isEnabled() && action(name(b))->isEnabled());
    }

    // Choosing the path's keys is an undo step (each change, as the pixels' selection is; a key dragged after is one more),
    // and a click on nothing chooses none with any tool, not only the selection ones.
    void choosingPathKeysIsUndoneAndAClickOnNothingChoosesNone()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        QVERIFY(controller.setCelKey(trajectory, 2, at(0, 0)));
        QVERIFY(controller.setCelKey(trajectory, 12, at(200, 100)));
        QVERIFY(controller.setCelKey(trajectory, 22, at(400, 0)));

        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setTrajectoryMode(true);
        const canvas::TrajectoryGizmo &gizmo = canvas.trajectoryGizmo();
        const QTransform toView = canvas.view().documentToView();
        auto screen = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        const double px = controller.document().cel(trajectory)->motion().pivotX;
        const double py = controller.document().cel(trajectory)->motion().pivotY;
        auto click = [&](QPoint at, Qt::KeyboardModifiers modifiers = {}) {
            QTest::mousePress(&canvas, Qt::LeftButton, modifiers, at);
            QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, at);
        };

        state.setActiveTool(tools::ToolId::RectSelect);
        click(screen(px + 200, py + 100));
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12}));
        click(screen(px + 400, py), Qt::ControlModifier);
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12, 22}));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo selection"));
        controller.undo();
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12}));
        controller.undo();
        QVERIFY(gizmo.chosenKeys().empty());
        controller.redo();
        controller.redo();
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12, 22}));
        click(screen(px + 400, py)); // chosen already: no step (the others stay, to drag them together)
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12, 22}));
        controller.undo(); // the Ctrl+click before it
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12}));
        controller.redo();
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12, 22}));

        // Another tool than the selection ones: a click on nothing chooses none, and that is undone too.
        state.setActiveTool(tools::ToolId::Move);
        click(screen(px + 100, py + 300));
        QVERIFY(gizmo.chosenKeys().empty());
        controller.undo();
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12, 22}));
    }

    // The whole path of a trajectory is shown on the page, from its first key to its last: while the trajectory is the
    // current cel, and while a cel that follows it is (the path is the trajectory's). The camera selected, or an inactive
    // trajectory: none.
    void theWholePathOfATrajectoryIsShown()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        doc::CelPose pose;
        pose.x = 0;
        pose.y = 0;
        QVERIFY(controller.setCelKey(trajectory, 2, pose));
        pose.x = 200;
        pose.y = 100;
        QVERIFY(controller.setCelKey(trajectory, 12, pose));
        pose.x = 400;
        pose.y = 0;
        QVERIFY(controller.setCelKey(trajectory, 22, pose));
        controller.addCel();
        const doc::CelId follower = controller.currentCel();
        QVERIFY(controller.setTrajectoryParent(follower, trajectory));

        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush);
        const canvas::TrajectoryGizmo &gizmo = canvas.trajectoryGizmo();
        const canvas::ViewTransform &view = canvas.view();

        // Off by default: no path. Trajectory mode shows it.
        QVERIFY(!state.trajectoryMode());
        QVERIFY(!gizmo.pathTrajectory());
        state.setTrajectoryMode(true);

        // The follower is selected: the path shown is the trajectory's.
        QCOMPARE(gizmo.pathTrajectory(), controller.document().cel(trajectory));
        int first = 0, last = 0;
        QVERIFY(gizmo.pathRange(*controller.document().cel(trajectory), &first, &last));
        QCOMPARE(first, 2);
        QCOMPARE(last, 22);
        const double px = controller.document().cel(trajectory)->motion().pivotX;
        const double py = controller.document().cel(trajectory)->motion().pivotY;
        QCOMPARE(gizmo.pathPoint(*controller.document().cel(trajectory), 12), QPointF(px + 200, py + 100));

        // It is drawn: ink at the key at frame 12 (the dot), seen from the follower and from the trajectory itself.
        auto inkAtKey = [&] {
            QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::black);
            QPainter painter(&image);
            gizmo.paintPath(painter, view);
            painter.end();
            const QPoint at = view.documentToView().map(QPointF(px + 200, py + 100)).toPoint();
            return image.pixelColor(at) != QColor(Qt::black);
        };
        QVERIFY(inkAtKey());
        controller.chooseCel(trajectory);
        QCOMPARE(gizmo.pathTrajectory(), controller.document().cel(trajectory));
        QVERIFY(inkAtKey());

        // A key is dragged along the page by its dot, with the follower (not the trajectory) the current cel: the position
        // of that key goes where the pen takes it, in one undo step; the key's frame stays.
        controller.chooseCel(follower);
        const QTransform toView = view.documentToView();
        auto at = [&](const QPointF &docPoint) { return toView.map(docPoint).toPoint(); };
        const QPointF keyDot(px + 200, py + 100);
        QCOMPARE(gizmo.handleAt(keyDot, view), canvas::TrajectoryGizmo::Handle::Key);
        QCOMPARE(gizmo.handleAt(QPointF(px + 100, py + 50), view), canvas::TrajectoryGizmo::Handle::None); // on the line
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(keyDot));
        QTest::mouseMove(&canvas, at(keyDot + QPointF(30, 20)));
        QTest::mouseMove(&canvas, at(keyDot + QPointF(60, 40)));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(keyDot + QPointF(60, 40)));
        QVERIFY(controller.celMotion(trajectory)->keyAt(doc::Channel::Position, 12));
        QVERIFY(qAbs(controller.celPoseAt(trajectory, 12).x - 260) < 2 && qAbs(controller.celPoseAt(trajectory, 12).y - 140) < 2);
        QVERIFY(qAbs(controller.celPoseAt(trajectory, 2).x) < 1e-6); // the other keys stay
        QCOMPARE(controller.currentCel(), follower);
        controller.undo();
        QVERIFY(qAbs(controller.celPoseAt(trajectory, 12).x - 200) < 1e-6);

        // A locked trajectory's keys are not dragged.
        auto lock = [&](bool on) {
            doc::CelProperties p = controller.document().cel(trajectory)->properties();
            p.locked = on;
            controller.execute(doc::makeSetCelProperties(controller.document(), trajectory, p));
        };
        lock(true);
        QCOMPARE(gizmo.handleAt(keyDot, view), canvas::TrajectoryGizmo::Handle::None);
        lock(false);

        // Keys dragged on the timeline show on the path live, with no edit: the key of frame 12 and the one of 22 would go
        // 3 frames on, the path's range and points follow; a move that is refused (a place taken) shows nothing moved.
        controller.chooseCel(follower);
        const doc::Cel &trajectoryCel = *controller.document().cel(trajectory);
        using Ref = document::DocumentController::CelKeyRef;
        controller.previewKeyMove({Ref{trajectory, 12, doc::Channel::Position}, Ref{trajectory, 22, doc::Channel::Position}}, 3);
        QVERIFY(controller.previewedMotion(trajectory));
        QCOMPARE(gizmo.pathPoint(trajectoryCel, 15), QPointF(px + 200, py + 100));
        QVERIFY(gizmo.pathRange(trajectoryCel, &first, &last));
        QCOMPARE(last, 25);
        QVERIFY(controller.celMotion(trajectory)->keyAt(doc::Channel::Position, 12)); // nothing was edited
        controller.previewKeyMove({Ref{trajectory, 12, doc::Channel::Position}}, -10); // onto frame 2, a place taken
        QVERIFY(!controller.previewedMotion(trajectory));
        controller.previewKeyMove({Ref{trajectory, 12, doc::Channel::Position}}, 4);
        controller.clearKeyMovePreview();
        QVERIFY(!controller.previewedMotion(trajectory));
        QVERIFY(gizmo.pathRange(trajectoryCel, &first, &last));
        QCOMPARE(last, 22);

        // Another cel with no trajectory: none.
        controller.addCel();
        QVERIFY(!gizmo.pathTrajectory());
        QVERIFY(!inkAtKey());

        // Mode off again: the path is gone, and its keys are not taken by the pen.
        controller.chooseCel(follower);
        QVERIFY(gizmo.pathShown());
        state.setTrajectoryMode(false);
        QVERIFY(!gizmo.pathShown());
        QCOMPARE(gizmo.handleAt(keyDot, view), canvas::TrajectoryGizmo::Handle::None);
        QVERIFY(!inkAtKey());

        // On, the pen does not draw while a path is shown (the brush leaves nothing).
        state.setTrajectoryMode(true);
        controller.chooseCel(follower);
        const QPoint far = toView.map(QPointF(300, 900)).toPoint();
        QTest::mousePress(&canvas, Qt::LeftButton, {}, far);
        QTest::mouseMove(&canvas, far + QPoint(30, 5));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, far + QPoint(30, 5));
        QVERIFY(!controller.currentDrawing() || controller.currentDrawing()->layers.empty()
                || controller.currentDrawing()->layers.front().image.isEmpty());
        // The same stroke with the mode off does draw.
        state.setTrajectoryMode(false);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, far);
        QTest::mouseMove(&canvas, far + QPoint(30, 5));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, far + QPoint(30, 5));
        QVERIFY(controller.currentDrawing() && !controller.currentDrawing()->layers.empty()
                && !controller.currentDrawing()->layers.front().image.isEmpty());
    }

    // Trajectories that follow one another: a child goes where its parent takes it, its own pose on top; an inactive one does
    // nothing of its own; no loops; a parent that goes leaves its children free (and back, with the undo).
    void trajectoriesFollowTheirParents()
    {
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId parent = controller.currentCel();
        controller.addTrajectory();
        const doc::CelId child = controller.currentCel();
        controller.addTrajectory();
        const doc::CelId grandchild = controller.currentCel();
        const doc::Document &d = controller.document();
        QVERIFY(controller.setTrajectoryParent(child, parent));
        QVERIFY(controller.setTrajectoryParent(grandchild, child));
        QVERIFY(!controller.setTrajectoryParent(parent, grandchild)); // a loop
        QVERIFY(!controller.setTrajectoryParent(parent, parent));
        QVERIFY(!controller.setTrajectoryParent(child, parent)); // nothing changes
        controller.undo();
        controller.undo(); // the grandchild's, then the child's
        QVERIFY(d.trajectoryParent(*d.cel(child)) == nullptr);
        controller.redo();
        controller.redo();
        QCOMPARE(d.trajectoryParent(*d.cel(grandchild))->id(), child);

        auto at = [&](doc::CelId id, int frame) { // where the circle of a trajectory is: its pivot through its matrix
            double x = 0, y = 0;
            d.trajectoryMatrix(id, frame).map(d.cel(id)->motion().pivotX, d.cel(id)->motion().pivotY, x, y);
            return QPointF(x, y);
        };
        QCOMPARE(at(grandchild, 0), QPointF(960, 540)); // nothing moves yet

        doc::CelPose slide;
        slide.x = 100;
        controller.setCelKey(parent, 0, slide);
        QCOMPARE(at(parent, 0), QPointF(1060, 540));
        QCOMPARE(at(child, 0), QPointF(1060, 540)); // the child goes along
        QCOMPARE(at(grandchild, 0), QPointF(1060, 540));
        doc::CelPose own;
        own.y = 50;
        controller.setCelKey(child, 0, own); // its own movement, on top
        QCOMPARE(at(child, 0), QPointF(1060, 590));
        QCOMPARE(at(grandchild, 0), QPointF(1060, 590));

        // The parent turned a quarter (clockwise, round its own pivot): the child, which stands 100 to the right of it
        // (it was moved there), goes round it: below it.
        controller.removeCelKey(parent, 0);
        doc::CelPose turn;
        turn.rotation = 90;
        controller.setCelKey(parent, 0, turn);
        controller.setCelKey(child, 0, [] { doc::CelPose p; p.x = 100; return p; }());
        const QPointF below = at(child, 0); // (960 + 100, 540) turned a quarter round (960, 540): (960, 640)
        QVERIFY(qAbs(below.x() - 960) < 0.01 && qAbs(below.y() - 640) < 0.01);

        // Inactive: the parent's own pose does nothing (the child is then moved only by its own).
        doc::CelProperties off = d.cel(parent)->properties();
        off.visible = false;
        controller.execute(doc::makeSetCelProperties(d, parent, off));
        QVERIFY(qAbs(at(child, 0).x() - 1060) < 0.01 && qAbs(at(child, 0).y() - 540) < 0.01);

        // The parent removed: the child stands alone, and follows again if that is undone.
        controller.execute(std::make_unique<doc::RemoveCelCommand>(parent));
        QVERIFY(d.trajectoryParent(*d.cel(child)) == nullptr);
        controller.undo();
        QVERIFY(d.trajectoryParent(*d.cel(child)) != nullptr);
    }

    // The circle of a trajectory that follows another is where the parent puts it, and dragging it changes only its own pose.
    void theCircleOfAChildIsDraggedInItsOwnPose()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId parent = controller.currentCel();
        controller.addTrajectory();
        const doc::CelId child = controller.currentCel();
        QVERIFY(controller.setTrajectoryParent(child, parent));
        doc::CelPose slide;
        slide.x = 200;
        controller.setCelKey(parent, 0, slide); // the parent stands 200 to the right
        controller.chooseCel(child);
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const canvas::TrajectoryGizmo &gizmo = canvas.trajectoryGizmo();
        const canvas::ViewTransform &view = canvas.view();
        const QTransform toView = view.documentToView();
        auto at = [&](const QPointF &docPoint) { return toView.map(docPoint).toPoint(); };
        const auto middle = gizmo.handlePoint(canvas::TrajectoryGizmo::Handle::Move, view);
        QVERIFY(middle && qAbs(middle->x() - 1160) < 1 && qAbs(middle->y() - 540) < 1); // where the parent takes it
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(*middle));
        QTest::mouseMove(&canvas, at(*middle + QPointF(30, 20)));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(*middle + QPointF(60, 40)));
        // Its own pose is the move, the parent's is untouched, and the circle went where the pen took it.
        QVERIFY(qAbs(controller.celPoseAt(child, 0).x - 60) < 1.5 && qAbs(controller.celPoseAt(child, 0).y - 40) < 1.5);
        QCOMPARE(controller.celPoseAt(parent, 0).x, 200.0);
        const auto now = gizmo.handlePoint(canvas::TrajectoryGizmo::Handle::Move, view);
        QVERIFY(now && qAbs(now->x() - 1220) < 1.5 && qAbs(now->y() - 580) < 1.5);
        // An inactive trajectory is shown but not taken.
        doc::CelProperties off = controller.document().cel(child)->properties();
        off.visible = false;
        controller.execute(doc::makeSetCelProperties(controller.document(), child, off));
        QVERIFY(!gizmo.editable());
    }

    // Fill looking at the other layers on a moved cel: the area is found in what is shown, and filled where it is in the
    // cel's own pixels (it used to find it in the page's pixels and fill the cel's: a stain on the background).
    void aFillReferringToWhatIsShownFillsInsideTheMovedDrawing()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage ring(120, 80, QImage::Format_ARGB32_Premultiplied);
        ring.fill(Qt::transparent);
        {
            QPainter p(&ring);
            p.setPen(QPen(Qt::black, 4));
            p.drawRect(2, 2, 115, 75); // a box drawn with a line, empty inside
        }
        controller.importImage(ring, QStringLiteral("box")); // x 900..1019, y 500..579
        const doc::CelId cel = controller.currentCel();
        controller.setCelKey(cel, 0, [] { doc::CelPose p; p.x = 300; p.y = 100; return p; }()); // shown at x 1200.., y 600..
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        tools::FillSettings fill = state.fillSettings();
        fill.allLayers = true;
        state.setFillSettings(fill);
        state.setActiveTool(tools::ToolId::Fill);
        state.setColor(QColor(0, 0, 255));
        const QTransform toView = canvas.view().documentToView();
        QTest::mouseClick(&canvas, Qt::LeftButton, {}, toView.map(QPointF(1260, 640)).toPoint()); // inside the box as it is shown
        const doc::TiledImage &pixels = controller.document().cel(cel)->drawingAt(0)->layers.front().image;
        QVERIFY(qAlpha(pixels.pixel(960, 540)) > 200 && qBlue(pixels.pixel(960, 540)) > 200); // inside, in the cel's own pixels
        QCOMPARE(qAlpha(pixels.pixel(300, 300)), 0);   // nothing on the background
        QCOMPARE(qAlpha(pixels.pixel(1260, 640)), 0);  // nor where the page's pixel of the click is
    }

    // The autoplay shows a cel moved by its keys frame by frame, though its drawing is held over all of them (it used to
    // draw again only when the drawings shown changed, so the picture stood still).
    void theAutoplayFollowsTheKeys()
    {
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece")); // x 930..989, held on every frame
        const doc::CelId cel = controller.currentCel();
        controller.setCelKey(cel, 0, doc::CelPose{});
        controller.setCelKey(cel, 10, [] { doc::CelPose p; p.x = 300; return p; }());
        controller.setCelKeyEasing(cel, 0, doc::Easing::linear());
        ui::AutoplayWindow window(&controller);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.setLoopRange(0, 10);
        auto *slider = window.findChild<QSlider *>();
        QVERIFY(slider);
        slider->setValue(0);
        QCOMPARE(window.image().pixelColor(960, 540), QColor(Qt::red));
        slider->setValue(10); // 300 pixels on: the piece is at x 1230..1289
        QCOMPARE(window.frame(), 10);
        QCOMPARE(window.image().pixelColor(960, 540), QColor(Qt::white));
        QCOMPARE(window.image().pixelColor(1260, 540), QColor(Qt::red));
        slider->setValue(5); // halfway
        QCOMPARE(window.image().pixelColor(1110, 540), QColor(Qt::red));
    }

    // Position, rotation, scale and opacity are keyed on their own: a channel with no key stays as drawn, one with keys goes
    // along its own, and the keys of one channel do not touch another's.
    void theChannelsAreKeyedOnTheirOwn()
    {
        doc::CelMotion motion;
        motion.setKey(doc::Channel::Position, 0, at(0, 0), doc::Easing::linear());
        motion.setKey(doc::Channel::Position, 10, at(100, 0), doc::Easing::linear());
        motion.setKey(doc::Channel::Rotation, 4, at(0, 0, 90), doc::Easing::linear());
        motion.setKey(doc::Channel::Rotation, 8, at(0, 0, 180), doc::Easing::linear());
        QVERIFY(!motion.hasKeys(doc::Channel::Scale) && !motion.hasKeys(doc::Channel::Opacity));
        QCOMPARE(motion.keyFrames(), (std::vector<int>{0, 4, 8, 10}));
        QVERIFY(motion.hasKeyAt(4) && !motion.keyAt(doc::Channel::Position, 4)); // a key of the rotation only
        const doc::CelPose six = motion.poseAt(6);
        QCOMPARE(six.x, 60.0);              // along the position's own keys
        QCOMPARE(six.rotation, 135.0);      // along the rotation's
        QCOMPARE(six.scaleX, 1.0);          // no key: as drawn
        QCOMPARE(six.opacity, 1.0);
        QCOMPARE(motion.poseAt(2).rotation, 90.0); // before the first rotation key: as it puts it
        QCOMPARE(motion.poseAt(30).rotation, 180.0);
        // A key keeps only what is of its channel.
        QCOMPARE(motion.keyAt(doc::Channel::Rotation, 4)->pose, doc::channelOf(doc::Channel::Rotation, at(55, 66, 90, 3, 0.2)));
        // The keys of all the channels at once on a frame: moved only if every place is free.
        motion.setKey(doc::Channel::Scale, 4, at(0, 0, 0, 2), doc::Easing::linear());
        motion.setKey(doc::Channel::Rotation, 5, at(0, 0, 100));
        QVERIFY(!motion.moveKeysAt(4, 5)); // the rotation has a key on 5: none of them moves
        QVERIFY(motion.keyAt(doc::Channel::Rotation, 4) && motion.keyAt(doc::Channel::Scale, 4));
        QVERIFY(motion.moveKeysAt(4, 6));
        QVERIFY(motion.keyAt(doc::Channel::Rotation, 6) && motion.keyAt(doc::Channel::Scale, 6));
        QVERIFY(motion.removeKeysAt(6));
        QVERIFY(!motion.hasKeys(doc::Channel::Scale) && motion.keys(doc::Channel::Rotation).size() == 2);
    }

    // The Cel key and what the tools do keep to the channels: the first key is of position, rotation and scale; a drag keys
    // (or changes) only what it changes; the key of a frame goes with its channels.
    void theControllerKeysChannels()
    {
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece"));
        const doc::CelId cel = controller.currentCel();
        QVERIFY(controller.setCelKey(cel, 0, doc::CelPose{}));
        const doc::CelMotion *m = controller.celMotion(cel);
        QVERIFY(m->hasKeys(doc::Channel::Position) && m->hasKeys(doc::Channel::Rotation) && m->hasKeys(doc::Channel::Scale));
        QVERIFY(!m->hasKeys(doc::Channel::Opacity)); // not unless it is used
        // A key on another frame with the opacity changed: the channels that have keys, and the opacity, which changed.
        QVERIFY(controller.setCelKey(cel, 10, at(50, 0, 0, 1, 0.5)));
        QVERIFY(m->keyAt(doc::Channel::Opacity, 10) && !m->keyAt(doc::Channel::Opacity, 0));
        QCOMPARE(controller.celPoseAt(cel, 10).opacity, 0.5);
        // One channel's key on its own: no other is made.
        QVERIFY(controller.setCelChannelKey(cel, doc::Channel::Rotation, 5, at(0, 0, 45)));
        QVERIFY(m->keyAt(doc::Channel::Rotation, 5) && !m->keyAt(doc::Channel::Position, 5) && !m->keyAt(doc::Channel::Scale, 5));
        QVERIFY(controller.removeCelKey(cel, 5, doc::Channel::Rotation));
        QVERIFY(!m->hasKeyAt(5));
        // Setting a pose by hand: a key of the channel on its frame is changed, with none the pose goes with the frame.
        controller.setCelPose(cel, 10, at(80, 0), doc::Channel::Position);
        QCOMPARE(controller.celPoseAt(cel, 10).x, 80.0);
        QVERIFY(m->keyAt(doc::Channel::Position, 10)->pose.x == 80.0);
        controller.setCelPose(cel, 3, at(0, 0, 30), doc::Channel::Rotation);
        QCOMPARE(controller.celPoseAt(cel, 3).rotation, 30.0);
        QVERIFY(!m->keyAt(doc::Channel::Rotation, 3)); // not a key
        QCOMPARE(controller.celPoseAt(cel, 3).x, controller.celMotion(cel)->interpolatedAt(3).x); // the rest as the keys have it
        controller.setCurrentFrame(4);
        QCOMPARE(controller.celPoseAt(cel, 3).rotation, 0.0); // gone with the frame
        // The easing and the move are for a channel, or for all the keys of the frame.
        QVERIFY(controller.setCelKeyEasing(cel, 10, doc::Easing::hold()));
        QCOMPARE(m->keyAt(doc::Channel::Position, 10)->easing, doc::Easing::hold());
        QCOMPARE(m->keyAt(doc::Channel::Opacity, 10)->easing, doc::Easing::hold());
        QVERIFY(controller.setCelKeyEasing(cel, 10, doc::Easing::linear(), doc::Channel::Opacity));
        QCOMPARE(m->keyAt(doc::Channel::Position, 10)->easing, doc::Easing::hold());
        QVERIFY(controller.moveCelKey(cel, 10, 12, doc::Channel::Opacity));
        QVERIFY(m->keyAt(doc::Channel::Position, 10) && m->keyAt(doc::Channel::Opacity, 12));
        QVERIFY(controller.moveCelKey(cel, 10, 11)); // the rest of them
        QVERIFY(!m->keyAt(doc::Channel::Position, 10) && m->keyAt(doc::Channel::Position, 11));
    }

    // The Transform tool keys what it changes: a drag moves the position's key, and the rotation and scale's stay.
    void aDragChangesOnlyItsOwnChannel()
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
        auto at2 = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        state.setActiveTool(tools::ToolId::Transform);
        controller.setCelKey(cel, 0, [] { doc::CelPose p; p.rotation = 20; p.scaleX = p.scaleY = 1.5; return p; }());
        // The key of frame 0 moves: the position's key is changed, the rotation's and the scale's are as they were.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at2(960, 540));
        QTest::mouseMove(&canvas, at2(1000, 540));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at2(1000, 540));
        canvas.finishTransform(true);
        state.setActiveTool(tools::ToolId::Transform); // (OK went back to the tool used before)
        const doc::CelMotion *m = controller.celMotion(cel);
        QVERIFY(m->keyAt(doc::Channel::Position, 0)->pose.x > 20);
        QCOMPARE(m->keyAt(doc::Channel::Rotation, 0)->pose.rotation, 20.0);
        QCOMPARE(m->keyAt(doc::Channel::Scale, 0)->pose.scaleX, 1.5);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        // On a frame with no key the drag leaves a pose that goes with the frame, of the position alone.
        controller.setCurrentFrame(7);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at2(1000, 540));
        QTest::mouseMove(&canvas, at2(1060, 540));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at2(1060, 540));
        QVERIFY(controller.celPoseAt(cel, 7).x > m->keyAt(doc::Channel::Position, 0)->pose.x + 30);
        QCOMPARE(controller.celPoseAt(cel, 7).rotation, 20.0);
        controller.toggleCelKey(cel); // the Cel key makes it a key of the channels that have keys
        QVERIFY(m->keyAt(doc::Channel::Position, 7) && m->keyAt(doc::Channel::Rotation, 7) && m->keyAt(doc::Channel::Scale, 7));
        QVERIFY(!m->keyAt(doc::Channel::Opacity, 7));
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
        QVERIFY(!canvas.transformBounds().isNull()); // the box is there as soon as the tool is chosen, as the pixels' is
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
        canvas.finishTransform(false); // Cancel: back to the tool used before
        QVERIFY(canvas.transformBounds().isNull());
        QVERIFY(state.activeTool() != tools::ToolId::Transform);
        QCOMPARE(controller.celPoseAt(cel, 0).x, 0.0);
        state.setActiveTool(tools::ToolId::Transform);
        drag(960, 1000);
        drag(1000, 1040);
        canvas.finishTransform(true); // OK: one step for both drags
        QVERIFY(canvas.transformBounds().isNull());
        QCOMPARE(controller.celPoseAt(cel, 0).x, moved);
        controller.undo();
        QCOMPARE(controller.celPoseAt(cel, 0).x, 0.0);
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1)); // the key stays: it was one step before
        // Leaving the frame applies an edit that was open.
        state.setActiveTool(tools::ToolId::Transform);
        drag(960, 1000);
        controller.setCurrentFrame(3);
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
        QVERIFY(controller.celMotion(cel)->hasKeyAt(0));
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
        // The same with a pixel tool: the cel is shown moved, and so are its ghosts.
        state.setActiveTool(tools::ToolId::Brush);
        QTest::qWait(50);
        QVERIFY(bluish(1180));
        QVERIFY(!bluish(960));
    }

    // A deformation cel pasted is bent by the bones it had, not by none.
    void aPastedDeformerKeepsItsBones()
    {
        document::DocumentController controller;
        controller.addDeformer(doc::DeformerKind::Bones);
        const doc::CelId bones = controller.currentCel();
        QCOMPARE(controller.addBone(bones, -1, {100, 100}, {200, 100}), 0);
        QCOMPARE(controller.addBone(bones, 0, {200, 100}, {300, 100}), 1);
        controller.copyCel(bones);
        const doc::CelId pasted = controller.pasteCel();
        QVERIFY(pasted != doc::kNoId && pasted != bones);
        QCOMPARE(controller.document().cel(pasted)->deformer().bones.size(), size_t(2));
        QVERIFY(controller.document().cel(pasted)->deformer() == controller.document().cel(bones)->deformer());
    }

    // Raster into new Cel: what the keys make of the cel is drawn as pictures in a new cel above it, without keys; a stretch
    // that looks the same stays one held picture; one undo step.
    void rasterCelBakesTheKeys()
    {
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece")); // x 930..989, y 520..559, one drawing for the scene
        const doc::CelId cel = controller.currentCel();
        doc::CelPose far;
        far.x = 100; // key 0 at the drawn place, key 10 a hundred pixels on, then it stays
        controller.setCelKey(cel, 0, doc::CelPose{});
        controller.setCelKey(cel, 10, far);
        controller.setCelKeyEasing(cel, 0, doc::Easing::linear());
        const int cels = controller.document().celCount();

        QString why;
        const doc::CelId raster = controller.rasterCel(cel, &why);
        QVERIFY2(raster != doc::kNoId, qPrintable(why));
        QCOMPARE(controller.document().celCount(), cels + 1);
        QCOMPARE(controller.document().indexOf(raster), controller.document().indexOf(cel) + 1); // above the cel
        const doc::Cel *made = controller.document().cel(raster);
        QVERIFY(made->motion().isEmpty()); // no keys
        QCOMPARE(made->timing().exposures().size(), size_t(11)); // frames 0-9 each, 10-29 the same one held
        QCOMPARE(made->timing().exposureAt(12)->start, 10);
        QCOMPARE(made->timing().exposureAt(12)->length, 20);
        auto covered = [&](int frame, int x) {
            const doc::Drawing *drawing = made->drawingAt(frame);
            return drawing && (controller.document().layer(raster, drawing->id, drawing->layers[0].id)->image.pixel(x, 540) >> 24) != 0;
        };
        QVERIFY(covered(0, 960) && !covered(0, 1040));   // as drawn
        QVERIFY(covered(5, 1010) && !covered(5, 940));    // half way (x 50): 980..1039
        QVERIFY(covered(15, 1070) && !covered(15, 950));  // after the last key (x 100)
        QVERIFY(controller.document().cel(cel)->motion().keyFrames().size() == 2); // the original is as it was

        controller.undo(); // all of it, one step
        QCOMPARE(controller.document().celCount(), cels);
        QVERIFY(controller.rasterCel(doc::kNoId, &why) == doc::kNoId);
    }

    // Raster into new Cel takes what the cel follows too: a trajectory that moves it, a deformer that bends it.
    void rasterCelBakesWhatTheCelFollows()
    {
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(60, 40, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("piece")); // x 930..989, y 520..559, one drawing for the scene
        const doc::CelId cel = controller.currentCel();
        controller.addTrajectory();
        const doc::CelId path = controller.currentCel();
        doc::CelPose pose = controller.celPoseAt(path, 0);
        controller.setCelKey(path, 0, pose);
        pose.x += 100;
        controller.setCelKey(path, 10, pose);
        QVERIFY(controller.setTrajectoryParent(cel, path));
        QString why;
        const doc::CelId moved = controller.rasterCel(cel, &why);
        QVERIFY2(moved != doc::kNoId, qPrintable(why));
        const doc::Cel *made = controller.document().cel(moved);
        QCOMPARE(made->timing().exposures().size(), size_t(11)); // frames 0-9 each, 10-29 the same one held
        QVERIFY((made->drawingAt(0)->layers[0].image.pixel(940, 540) >> 24) != 0);
        QVERIFY((made->drawingAt(20)->layers[0].image.pixel(1070, 540) >> 24) != 0);
        QVERIFY((made->drawingAt(20)->layers[0].image.pixel(940, 540) >> 24) == 0);
        controller.undo();

        controller.setTrajectoryParent(cel, doc::kNoId);
        controller.setCurrentCel(cel);
        controller.addDeformer(doc::DeformerKind::Bones); // the cel is bent by it
        const doc::CelId bones = controller.currentCel();
        QCOMPARE(controller.addBone(bones, -1, {930, 540}, {990, 540}), 0);
        std::vector<double> bent = controller.document().cel(bones)->deformer().rest;
        QVERIFY(controller.setDeformKey(bones, 0, bent));
        for (double &v : bent)
            v += 0.5;
        QVERIFY(controller.setDeformKey(bones, 10, bent));
        const doc::CelId warped = controller.rasterCel(cel, &why);
        QVERIFY2(warped != doc::kNoId, qPrintable(why));
        QCOMPARE(controller.document().cel(warped)->timing().exposures().size(), size_t(11));
    }

    // Cels moved together on a frame with no key all go, not the last one only; leaving the frame takes them all back,
    // and the Cel key button keeps them all.
    void severalCelsMovedWhereTheyHaveNoKeyAllGo()
    {
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage red(60, 40, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("a"));
        const doc::CelId a = controller.currentCel();
        controller.importImage(red, QStringLiteral("b"));
        const doc::CelId b = controller.currentCel();
        for (doc::CelId id : {a, b})
            for (int frame : {0, 10})
                controller.setCelKey(id, frame, doc::CelPose{});
        controller.setCelSelection({a, b}, b);
        auto drag = [&](double x) {
            doc::CelPose pose;
            pose.x = x;
            controller.previewCelPoses({{a, 5, pose, {doc::Channel::Position}}, {b, 5, pose, {doc::Channel::Position}}});
        };
        controller.setCurrentFrame(5);
        drag(40);
        drag(77);
        QCOMPARE(controller.celPoseAt(a, 5).x, 77.0);
        QCOMPARE(controller.celPoseAt(b, 5).x, 77.0);
        controller.setCurrentFrame(6);
        QCOMPARE(controller.document().cel(a)->motion().hasTemporary(), false);
        QCOMPARE(controller.document().cel(b)->motion().hasTemporary(), false);
        controller.setCurrentFrame(5);
        drag(77);
        controller.toggleChosenCelKeys();
        for (doc::CelId id : {a, b}) {
            QVERIFY(controller.celMotion(id)->hasKeyAt(5));
            QCOMPARE(controller.celPoseAt(id, 5).x, 77.0);
        }
        // One cel posed alone: what was left on the other goes.
        controller.setCurrentFrame(3);
        drag(20);
        doc::CelPose alone;
        alone.x = 9;
        controller.previewCelPose(a, 3, alone, {doc::Channel::Position});
        QCOMPARE(controller.celPoseAt(a, 3).x, 9.0);
        QVERIFY(!controller.document().cel(b)->motion().hasTemporary());
    }

    // A vector layer bent by a deformer is exported (and made into a raster cel) as the canvas shows it: drawn again from
    // its strokes, not its pixels bent.
    void aBentVectorLayerIsExportedAsTheCanvasShowsIt()
    {
        tools::ToolState state;
        document::DocumentController controller;
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        controller.execute(doc::makeNewDrawing(d, cel, 0, 1, "Layer 1").command);
        QVERIFY(controller.addLayer(true, doc::LayerKind::Vector));
        doc::Layer *layer = d.layer(cel, controller.currentDrawingId(), controller.currentLayer());
        doc::VectorStroke line;
        line.size = 12;
        for (int i = 0; i <= 100; ++i) {
            brush::BrushInput in;
            in.x = 800 + i * 4;
            in.y = 540;
            in.pressure = 1;
            line.points.push_back(in);
        }
        layer->strokes = {line};
        canvas::drawStrokes(layer->image, layer->strokes, state, {});
        controller.addDeformer(doc::DeformerKind::Bones);
        const doc::CelId bones = controller.currentCel();
        QCOMPARE(controller.addBone(bones, -1, {800, 540}, {1000, 540}), 0);
        QCOMPARE(controller.addBone(bones, 0, {1000, 540}, {1200, 540}), 1);
        std::vector<double> pose = d.cel(bones)->deformer().rest;
        pose[doc::kBoneParams + doc::BoneAngle] = 70;
        QVERIFY(controller.setDeformKey(bones, 0, pose));

        canvas::FrameRenderer shown; // as the canvas has it
        shown.setBrushes(&state);
        shown.setGuidesVisible(false);
        shown.setResampleFilter(canvas::ResampleFilter::Bicubic);
        shown.renderAll(d, 0);
        QVERIFY(io::renderFrame(d, 0, false) != shown.image()); // no brushes known: the pixels bent
        canvas::FrameRenderer::setDefaultBrushes(&state);
        QCOMPARE(io::renderFrame(d, 0, false), shown.image());
        const doc::CelId raster = controller.rasterCel(cel);
        QVERIFY(raster != doc::kNoId);
        canvas::FrameRenderer baked;
        baked.setOnlyCel(raster);
        baked.renderAll(d, 0);
        QCOMPARE(baked.image(), shown.image());
        canvas::FrameRenderer::setDefaultBrushes(nullptr);
    }

    // A cel carried by a bone, given a deformer of its own, is bent by it.
    void aCarriedCelGivenADeformerIsBentByIt()
    {
        document::DocumentController controller;
        const doc::CelId hand = controller.currentCel();
        controller.addDeformer(doc::DeformerKind::Bones);
        const doc::CelId arm = controller.currentCel();
        QCOMPARE(controller.addBone(arm, -1, {0, 0}, {50, 0}), 0);
        QVERIFY(controller.setDeformerParent(hand, arm, 0));
        controller.setCurrentCel(hand);
        controller.addDeformer(doc::DeformerKind::Bones);
        const doc::CelProperties &p = controller.document().cel(hand)->properties();
        QCOMPARE(p.parent, controller.currentCel());
        QCOMPARE(p.parentPart, -1);
        controller.undo();
        QCOMPARE(p.parent, arm);
        QCOMPARE(p.parentPart, 0);
    }

    // Keys are pasted only on a cel that has their channel: not a position on a deformer, nor a deformer's pose on a drawn
    // cel or on a deformer with other bones.
    void keysArePastedOnlyWhereTheyBelong()
    {
        document::DocumentController controller;
        const doc::CelId drawn = controller.currentCel();
        controller.setCelKey(drawn, 0, doc::CelPose{});
        const auto position = controller.copyCelKeys({{drawn, 0, doc::Channel::Position}});
        auto bones = [&](int count) {
            controller.setCurrentCel(drawn);
            controller.addDeformer(doc::DeformerKind::Bones);
            const doc::CelId id = controller.currentCel();
            for (int i = 0; i < count; ++i)
                controller.addBone(id, i - 1, {i * 50.0, 0}, {i * 50.0 + 50, 0});
            return id;
        };
        const doc::CelId one = bones(1), two = bones(2), alike = bones(1);
        QVERIFY(!controller.pasteCelKeys(position, 0, one));
        QVERIFY(controller.celMotion(one)->isEmpty());
        std::vector<double> pose = controller.document().cel(one)->deformer().rest;
        QVERIFY(controller.setDeformKey(one, 0, pose));
        const auto deform = controller.copyCelKeys({{one, 0, doc::Channel::Deform}});
        QVERIFY(!controller.pasteCelKeys(deform, 5, drawn));
        QVERIFY(!controller.pasteCelKeys(deform, 5, two));
        QVERIFY(controller.pasteCelKeys(deform, 5, alike));
        QVERIFY(controller.celMotion(alike)->hasKeys(doc::Channel::Deform));
    }

    // A trajectory without keys (any more, or read from a file) has its circle in the middle of the page.
    void aTrajectoryWithoutKeysStaysOnThePage()
    {
        document::DocumentController controller;
        controller.addTrajectory();
        const doc::CelId id = controller.currentCel();
        const double x = controller.document().width() / 2.0, y = controller.document().height() / 2.0;
        auto middle = [&](const doc::CelMotion &m) { return m.isEmpty() && m.pivotX == x && m.pivotY == y; };
        QVERIFY(middle(*controller.celMotion(id)));
        controller.setCelKey(id, 0, controller.celPoseAt(id, 0));
        QVERIFY(controller.clearCelMotion(id));
        QVERIFY(middle(*controller.celMotion(id)));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("t.moth"));
        QString error;
        QVERIFY2(io::saveProject(controller.document(), path, &error), qPrintable(error));
        const auto loaded = io::loadProject(path, &error);
        QVERIFY2(loaded, qPrintable(error));
        QVERIFY(middle(loaded->cel(id)->motion()));
    }

    // Cels chosen together are moved by the Transform tool as one piece: a drag on the box does to each of them what it does to
    // the box, in one undo step; a cel with no keys gets its first one.
    void severalChosenCelsAreMovedTogether()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage red(60, 40, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("a")); // x 930..989, y 520..559
        const doc::CelId a = controller.currentCel();
        QImage green(40, 40, QImage::Format_ARGB32);
        green.fill(Qt::green);
        controller.importImage(green, QStringLiteral("b")); // x 940..979, y 520..559
        const doc::CelId b = controller.currentCel();
        const doc::CelId c = [&] { // a third, with no keys at all
            controller.importImage(green, QStringLiteral("c"));
            return controller.currentCel();
        }();
        controller.setCelKey(a, 0, doc::CelPose{});
        controller.setCelKey(b, 0, doc::CelPose{});
        controller.setCelSelection({a, b, c}, b);
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Transform);
        QVERIFY(state.keyframeMode());
        const QTransform toView = canvas.view().documentToView();
        const double zoom = canvas.view().zoom();
        auto at = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };

        // Inside the box: all of them move by as much as the pen did, in one step.
        QSignalSpy motion(&controller, &document::DocumentController::motionChanged);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(960, 540));
        motion.clear();
        QTest::mouseMove(&canvas, at(1000, 560));
        QCOMPARE(motion.size(), 1); // all the cels are told at once: the frame is drawn once for a move, not once per cel
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1010, 570));
        for (doc::CelId id : {a, b, c}) {
            const doc::CelPose pose = controller.celPoseAt(id, 0);
            QVERIFY2(std::abs(pose.x - 50) < 1.5 / zoom + 0.6 && std::abs(pose.y - 30) < 1.5 / zoom + 0.6,
                     qPrintable(QStringLiteral("cel %1 at %2, %3").arg(id).arg(pose.x).arg(pose.y)));
        }
        QVERIFY(controller.celMotion(c)->hasKeyAt(0)); // the one with no keys got its first
        controller.undo(); // the whole drag, for all of them, is one step
        for (doc::CelId id : {a, b})
            QCOMPARE(controller.celPoseAt(id, 0).x, 0.0);
        QVERIFY(controller.celMotion(c)->isEmpty());
        controller.redo();
        QCOMPARE(controller.celPoseAt(b, 0).x, controller.celPoseAt(a, 0).x);

        // Outside the box: all of them turn, round the middle of the box.
        const doc::CelPose before = controller.celPoseAt(a, 0);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, at(1060 + 50, 570)); // to the right of the box (its middle is at 1010, 570)
        for (int angle = 15; angle <= 90; angle += 15)
            QTest::mouseMove(&canvas, at(1010 + 100 * std::cos(angle * M_PI / 180), 570 + 100 * std::sin(angle * M_PI / 180)));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, at(1010, 670));
        for (doc::CelId id : {a, b, c})
            QVERIFY2(std::abs(controller.celPoseAt(id, 0).rotation - 90) < 3,
                     qPrintable(QString::number(controller.celPoseAt(id, 0).rotation)));
        QVERIFY(before.rotation == 0);
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
        QVERIFY(controller.celMotion(cel)->hasKeyAt(0));
        QVERIFY(!key->isEnabled() && del->isEnabled());
        x->setValue(75); // on a key: the key changes
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1));
        QCOMPARE(controller.celPoseAt(cel, 0).x, 75.0);
        controller.setCurrentFrame(12);
        QCOMPARE(x->value(), 75.0); // after the last key it stays as it was put
        rotation->setValue(45); // on a frame with no key: a pose that goes with the frame
        scale->setValue(150);
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1));
        QCOMPARE(controller.celPoseAt(cel, 12).rotation, 45.0);
        QCOMPARE(controller.celPoseAt(cel, 12).scaleX, 1.5);
        key->click(); // made a key before leaving
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(2));
        QCOMPARE(controller.celPoseAt(cel, 12).rotation, 45.0);
        controller.setCurrentFrame(6);
        QVERIFY(x->value() == 75.0 && rotation->value() > 0 && rotation->value() < 45); // between: in between
        QVERIFY(!del->isEnabled());
        controller.setCurrentFrame(12);
        del->click();
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(1));
        // Changing the key's curve.
        controller.setCurrentFrame(0);
        auto *easing = panel.findChild<QComboBox *>(QStringLiteral("poseEasing"));
        easing->setCurrentIndex(easing->findText(QStringLiteral("Hold")));
        emit easing->activated(easing->currentIndex());
        QCOMPARE(controller.celMotion(cel)->keyAt(doc::Channel::Position, 0)->easing, doc::Easing::hold());
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
        QVERIFY(controller.celMotion(cel)->hasKeyAt(4));
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
        QVERIFY(controller.celMotion(cel)->hasKeyAt(9) && !controller.celMotion(cel)->hasKeyAt(4));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        // A double click on an empty cell of the row: a key there.
        QMouseEvent press(QEvent::MouseButtonDblClick, QPointF(diamond(20)), vp->mapToGlobal(QPointF(diamond(20))),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(diamond(20)), vp->mapToGlobal(QPointF(diamond(20))),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &release);
        QVERIFY(controller.celMotion(cel)->hasKeyAt(20));
        // The header of the row of keys has no toggles: a click on it chooses the cel and changes nothing else.
        const bool visible = controller.document().cel(cel)->properties().visible;
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(60, motionY));
        QCOMPARE(controller.document().cel(cel)->properties().visible, visible);
        QCOMPARE(controller.currentCel(), cel);
        // The rows of the channels are closed to begin with; the arrow at the right of the "Keyframes" header opens them under
        // it (the keys stay, and so does the row itself); another click closes them again.
        QVERIFY(!view->channelRowsShown(0));
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(m.headerWidth - 12, motionY));
        QVERIFY(view->channelRowsShown(0));
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(m.headerWidth - 12, motionY));
        QVERIFY(!view->channelRowsShown(0));
        QCOMPARE(controller.celMotion(cel)->keyFrames().size(), size_t(2));
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(m.headerWidth - 12, motionY));
        QVERIFY(view->channelRowsShown(0)); // open again
        // The arrow in the cel's own header folds "Keyframes" away, and the channels with it.
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(m.headerWidth - 12, m.rowY(0) + m.rowHeight / 2));
        QVERIFY(!view->keyframesRowShown(0) && !view->channelRowsShown(0));
        QTest::mouseClick(vp, Qt::LeftButton, {}, QPoint(m.headerWidth - 12, m.rowY(0) + m.rowHeight / 2));
        QVERIFY(view->keyframesRowShown(0) && view->channelRowsShown(0));
        QCOMPARE(controller.document().cel(cel)->properties().visible, visible); // the arrow is no toggle of the cel
        // The button takes the key of the current frame away, and the row goes with the last key.
        controller.setCurrentFrame(9);
        button->click();
        QVERIFY(!controller.celMotion(cel)->hasKeyAt(9));
        controller.setCurrentFrame(20);
        button->click();
        QVERIFY(controller.celMotion(cel)->isEmpty());
    }

    // S over the timeline with keys chosen scales them in time from the red cursor: a number typed is the factor, Enter
    // makes it (one undo step), Esc drops it; two keys on one frame are refused.
    void theChosenKeysAreScaledFromTheCursor()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(40);
        const doc::CelId cel = controller.currentCel();
        QVERIFY(controller.setCelKey(cel, 4, at(0, 0)));
        QVERIFY(controller.setCelKey(cel, 8, at(50, 0)));
        QVERIFY(controller.setCelKey(cel, 12, at(100, 0)));
        ui::TimelinePanel panel(&controller, &state);
        panel.resize(900, 260);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        ui::TimelineView *view = panel.view();
        ui::TimelineMetrics m;
        const int motionY = m.rowY(1) + m.rowHeight / 2;
        auto diamond = [&](int frame) { return QPoint(m.frameX(frame) + m.frameWidth / 2, motionY); };
        auto *vp = view->viewport();
        QTest::mouseClick(vp, Qt::LeftButton, {}, diamond(4));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, diamond(8));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, diamond(12));
        controller.setCurrentFrame(4);
        view->setFocus();
        auto frames = [&] { return controller.celMotion(cel)->keyFrames(); };
        const std::vector<int> before{4, 8, 12};
        QCOMPARE(frames(), before);

        QTest::keyClick(view, Qt::Key_S);
        QTest::keyClick(view, Qt::Key_2);
        QCOMPARE(frames(), before); // only shown until it is made
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(frames(), (std::vector<int>{4, 12, 20}));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));

        // Still chosen where they went: half as far again.
        QTest::keyClick(view, Qt::Key_S);
        QTest::keyClicks(view, QStringLiteral("0.5"));
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(frames(), before);

        // Esc drops it.
        QTest::keyClick(view, Qt::Key_S);
        QTest::keyClick(view, Qt::Key_3);
        QTest::keyClick(view, Qt::Key_Escape);
        QCOMPARE(frames(), before);

        // All on the cursor's frame: refused, nothing changes.
        QTest::keyClick(view, Qt::Key_S);
        QTest::keyClick(view, Qt::Key_0);
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(frames(), before);

        // From another frame: the keys go away from it, on both sides.
        controller.setCurrentFrame(8);
        QTest::keyClick(view, Qt::Key_S);
        QTest::keyClick(view, Qt::Key_2);
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(frames(), (std::vector<int>{0, 8, 16}));
        controller.undo();
        QCOMPARE(frames(), before);
    }

    // The keys of a path chosen on the canvas are chosen on the timeline too: Delete there takes them away.
    void pathKeysChosenOnTheCanvasAreChosenOnTheTimeline()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        QVERIFY(controller.setCelKey(trajectory, 2, at(0, 0)));
        QVERIFY(controller.setCelKey(trajectory, 12, at(200, 100)));
        QVERIFY(controller.setCelKey(trajectory, 22, at(400, 0)));

        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        ui::TimelinePanel panel(&controller, &state);
        panel.resize(900, 260);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        state.setTrajectoryMode(true);
        const QTransform toView = canvas.view().documentToView();
        const double px = controller.document().cel(trajectory)->motion().pivotX;
        const double py = controller.document().cel(trajectory)->motion().pivotY;
        auto screen = [&](double x, double y) { return toView.map(QPointF(x, y)).toPoint(); };
        auto click = [&](QPoint at, Qt::KeyboardModifiers modifiers = {}) {
            QTest::mousePress(&canvas, Qt::LeftButton, modifiers, at);
            QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, at);
        };
        controller.setCurrentFrame(17); // the circle away from the keys clicked
        click(screen(px, py));
        click(screen(px + 400, py), Qt::ControlModifier);
        QCOMPARE(canvas.trajectoryGizmo().chosenKeys(), (std::set<int>{2, 22}));

        QVERIFY(panel.view()->selectedKey().has_value());
        QCOMPARE(panel.view()->selectedKey()->second, 22);
        panel.view()->setFocus();
        QTest::keyClick(panel.view(), Qt::Key_Delete);
        QCOMPARE(controller.celMotion(trajectory)->keyFrames(), std::vector<int>{12});
    }

    // With the Transform tool and keys chosen on the path, a box round them moves (and scales) them together; one undo
    // step a drag.
    void theTransformToolMovesAndScalesTheChosenPathKeys()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        controller.addTrajectory();
        const doc::CelId trajectory = controller.currentCel();
        QVERIFY(controller.setCelKey(trajectory, 2, at(0, 0)));
        QVERIFY(controller.setCelKey(trajectory, 12, at(200, 100)));
        QVERIFY(controller.setCelKey(trajectory, 22, at(400, 0)));
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 520);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setTrajectoryMode(true);
        state.setActiveTool(tools::ToolId::Transform);
        controller.setCurrentFrame(0); // the circle on the key of 2, away from the box
        const canvas::TrajectoryGizmo &gizmo = canvas.trajectoryGizmo();
        const QTransform toView = canvas.view().documentToView();
        const double px = controller.document().cel(trajectory)->motion().pivotX;
        const double py = controller.document().cel(trajectory)->motion().pivotY;
        auto screen = [&](QPointF p) { return toView.map(p).toPoint(); };
        auto drag = [&](QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = {}) {
            QTest::mousePress(&canvas, Qt::LeftButton, modifiers, from);
            QTest::mouseMove(&canvas, (from + to) / 2);
            QTest::mouseMove(&canvas, to);
            QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, to);
        };
        auto near = [&](int frame, double x, double y) {
            const doc::CelPose p = controller.celPoseAt(trajectory, frame);
            return qAbs(p.x - x) < 3 && qAbs(p.y - y) < 3;
        };
        drag(screen({px + 200, py + 100}), screen({px + 200, py + 100}));
        QVERIFY(!gizmo.keyBoxShown()); // one key: no box
        drag(screen({px + 400, py}), screen({px + 400, py}), Qt::ControlModifier);
        QCOMPARE(gizmo.chosenKeys(), (std::set<int>{12, 22}));
        QVERIFY(gizmo.keyBoxShown());

        // Inside the box: both move as far, 2 stays.
        drag(screen({px + 300, py + 50}), screen({px + 350, py + 80}));
        QVERIFY(near(12, 250, 130) && near(22, 450, 30) && near(2, 0, 0));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo keyframes"));
        controller.undo();
        QVERIFY(near(12, 200, 100) && near(22, 400, 0));

        // Its right side, out twice as far from the left one: twice as wide.
        const QRectF box = gizmo.keyBox(canvas.view());
        const double left = box.left();
        drag(screen({box.right(), box.center().y()}), screen({left + 2 * box.width(), box.center().y()}));
        const double x12 = left + 2 * (px + 200 - left) - px, x22 = left + 2 * (px + 400 - left) - px;
        QVERIFY(near(12, x12, 100) && near(22, x22, 0));
    }
};

QTEST_MAIN(TestCelMotion)
#include "tst_celmotion.moc"
