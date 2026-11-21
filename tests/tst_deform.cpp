// Tests for the deformation cels' core: the bones' field and the bending of pixels.

#include "core/deform/BoneField.h"
#include "core/deform/DeformRaster.h"
#include "core/document/CelMotion.h"
#include "core/document/Document.h"

#include <QTest>

#include <cmath>

using namespace mothion;
using deform::Vec2;

namespace {

// A bone from (x, y) at `angle` degrees, `length` long, holding what is within `radius` (and `fade` more).
doc::Deformer oneBone(double x, double y, double angle, double length, double radius = 20, double fade = 10)
{
    doc::Deformer d;
    d.bones.push_back({-1, radius, fade});
    d.rest = {x, y, angle, length};
    return d;
}

// An arm: a bone from (0, 0) to (100, 0), and one from its end to (200, 0).
doc::Deformer arm()
{
    doc::Deformer d;
    d.bones.push_back({-1, 30, 20});
    d.bones.push_back({0, 30, 20});
    d.rest = {0, 0, 0, 100, 0, 0, 0, 100};
    return d;
}

bool near(Vec2 a, Vec2 b, double tolerance = 1e-6)
{
    return std::hypot(a.x - b.x, a.y - b.y) <= tolerance;
}

} // namespace

class TestDeform : public QObject
{
    Q_OBJECT

private slots:
    void bonesAreChainedFromTheirParentsEnds()
    {
        doc::Deformer d = arm();
        std::vector<double> pose = d.rest;
        pose[2] = 90;      // the upper arm turned down
        pose[4 + 2] = -90; // the forearm turned back from it: along the x axis again
        const auto frames = deform::boneFrames(d, pose);
        QVERIFY(near(frames[0].end(), {0, 100}));
        QVERIFY(near(frames[1].origin, {0, 100}));
        QVERIFY(near(frames[1].end(), {100, 100}));
        // A pose that lacks numbers has the rest's.
        QVERIFY(near(deform::boneFrames(d, {})[1].end(), {200, 0}));
    }

    void theRestMovesNothing()
    {
        const deform::BoneField field(arm(), arm().rest);
        QVERIFY(field.isIdentity());
        QVERIFY(near(field.map({37, 12}), {37, 12}));
    }

    void aBoneTurnsWhatItHoldsAndLeavesTheRest()
    {
        const doc::Deformer d = oneBone(0, 0, 0, 100);
        std::vector<double> pose = d.rest;
        pose[doc::BoneAngle] = 90;
        const deform::BoneField field(d, pose);
        QVERIFY(!field.isIdentity());
        QVERIFY(near(field.map({50, 0}), {0, 50}, 1e-6));   // on the bone: turned with it (clockwise, y down)
        QVERIFY(near(field.map({50, 10}), {-10, 50}, 1e-6)); // within its radius too
        QVERIFY(near(field.map({50, 500}), {50, 500}));       // out of its zone: as drawn
        // In the fade: between where it was and where the bone takes it.
        const Vec2 faded = field.map({50, 25});
        QVERIFY(faded.x > -25 && faded.x < 50);
    }

    void aMovedRootMovesWhatItHolds()
    {
        const doc::Deformer d = oneBone(0, 0, 0, 100);
        std::vector<double> pose = d.rest;
        pose[doc::BoneX] = 30;
        pose[doc::BoneY] = -5;
        QVERIFY(near(deform::BoneField(d, pose).map({60, 3}), {90, -2}));
    }

    void aLongerBoneStretchesAlongItself()
    {
        const doc::Deformer d = oneBone(0, 0, 0, 100);
        std::vector<double> pose = d.rest;
        pose[doc::BoneLength] = 150;
        const deform::BoneField field(d, pose);
        QVERIFY(near(field.map({50, 4}), {75, 4}));  // half way along: half way along the longer bone
        QVERIFY(near(field.map({110, 0}), {160, 0})); // past its end: moved on with it, not stretched
        QVERIFY(near(field.map({-10, 0}), {-10, 0})); // before its start: still
    }

    void aBentJointBendsSmoothlyAndDoesNotShrink()
    {
        const doc::Deformer d = arm();
        std::vector<double> pose = d.rest;
        pose[4 + doc::BoneAngle] = 90; // the forearm bent down
        const deform::BoneField field(d, pose);
        QVERIFY(near(field.map({20, 0}), {20, 0}, 0.5));     // the upper arm, far from the joint: still
        QVERIFY(near(field.map({190, 0}), {100, 90}, 0.5));  // the forearm's end: turned round the elbow
        // At the joint the bend is shared: a point just outside the elbow turns half way, at its distance from it.
        const Vec2 outside = field.map({100, -10});
        QVERIFY(std::abs(std::hypot(outside.x - 100, outside.y) - 10) < 0.5);
        QVERIFY(outside.x > 100 && outside.y < 0);
        // Along the arm, the points go on smoothly: no step from one sample to the next.
        Vec2 last = field.map({0, 0});
        for (int x = 1; x <= 200; ++x) {
            const Vec2 p = field.map({double(x), 0});
            QVERIFY2(std::hypot(p.x - last.x, p.y - last.y) < 2.0, qPrintable(QString::number(x)));
            last = p;
        }
    }

    void pixelsAreBentWhereTheBonesTakeThem()
    {
        // A bar 100 px long and 6 high along a bone, turned down by it.
        puppet::RasterImage bar(0, -3, 100, 6);
        std::fill(bar.pixels.begin(), bar.pixels.end(), 0xFF000000u);
        const doc::Deformer d = oneBone(0, 0, 0, 100);
        std::vector<double> pose = d.rest;
        pose[doc::BoneAngle] = 90;
        const deform::BoneField field(d, pose);
        const puppet::RasterImage bent = deform::warpImage(bar, [&](Vec2 p) { return field.map(p); });
        QVERIFY(!bent.isEmpty());
        // Now it stands: about 6 wide and 100 high, round x = 0.
        QVERIFY(bent.x <= -3 && bent.x + bent.width >= 3);
        QVERIFY(bent.y <= 0 && bent.y + bent.height >= 100);
        QVERIFY(bent.width <= 10);
        auto at = [&](int x, int y) { return bent.pixels[std::size_t(y - bent.y) * std::size_t(bent.width) + std::size_t(x - bent.x)]; };
        QCOMPARE(at(0, 50) >> 24, 255u);
        QCOMPARE(at(0, 5) >> 24, 255u);
        // Nothing where it was.
        QVERIFY(bent.x + bent.width <= 50);
    }

    void aDeformKeyInterpolatesItsNumbers()
    {
        doc::CelMotion motion;
        doc::CelPose a, b;
        a.deform = {0, 0, 0, 100};
        b.deform = {10, 0, 90, 100, 7};
        motion.setKey(doc::Channel::Deform, 0, a, doc::Easing::linear());
        motion.setKey(doc::Channel::Deform, 10, b);
        const std::vector<double> half = motion.poseAt(5).deform;
        QCOMPARE(half.size(), std::size_t(5));
        QCOMPARE(half[0], 5.0);
        QCOMPARE(half[2], 45.0);
        QCOMPARE(half[4], 7.0); // a number only one of them has stays as it has it
        QVERIFY(motion.hasKeyAt(10));
        QCOMPARE(motion.keyFrames().size(), std::size_t(2));
    }

    void aDrawnCelIsBentByItsDeformerAndFollowsItsTrajectory()
    {
        doc::Document d(100, 100);
        doc::CelProperties t{"Trajectory"};
        t.trajectory = true;
        auto &trajectory = d.insertCel(0, std::make_unique<doc::Cel>(d.allocateCelId(), t));
        doc::CelProperties b{"Bones"};
        b.deformer = doc::DeformerKind::Bones;
        b.parent = trajectory.id();
        auto &bones = d.insertCel(1, std::make_unique<doc::Cel>(d.allocateCelId(), b));
        doc::CelProperties c{"Arm"};
        c.parent = bones.id();
        auto &drawn = d.insertCel(2, std::make_unique<doc::Cel>(d.allocateCelId(), c));
        auto &other = d.insertCel(3, std::make_unique<doc::Cel>(d.allocateCelId(), doc::CelProperties{"Other"}));

        QCOMPARE(d.followedDeformer(drawn), &bones);
        QCOMPARE(d.followedTrajectory(drawn), &trajectory); // through its deformer
        QCOMPARE(d.followedTrajectory(bones), &trajectory);
        QCOMPARE(d.followedDeformer(bones), nullptr);
        // Only a drawn cel can be bent: not a trajectory, not another deformer.
        QVERIFY(!d.trajectoryLoop(other.id(), bones.id()));
        QVERIFY(d.trajectoryLoop(trajectory.id(), bones.id()));
        // A deformer can follow a trajectory, not a deformer.
        doc::CelProperties b2{"Bones 2"};
        b2.deformer = doc::DeformerKind::Bones;
        auto &bones2 = d.insertCel(4, std::make_unique<doc::Cel>(d.allocateCelId(), b2));
        QVERIFY(!d.trajectoryLoop(bones2.id(), trajectory.id()));
        QVERIFY(d.trajectoryLoop(bones2.id(), bones.id()));
        // An inactive deformer bends nothing.
        doc::CelProperties off = bones.properties();
        off.visible = false;
        bones.setProperties(off);
        QCOMPARE(d.followedDeformer(drawn), nullptr);
        QCOMPARE(d.followedTrajectory(drawn), &trajectory); // it still goes along the way
    }

    void theDeformersPoseIsItsKeysOrItsRest()
    {
        doc::Cel cel(1, doc::CelProperties{"Bones"});
        cel.setDeformer(oneBone(0, 0, 0, 50));
        doc::Document d(10, 10);
        QCOMPARE(d.deformPoseAt(cel, 3), cel.deformer().rest);
        doc::CelMotion motion;
        doc::CelPose p;
        p.deform = {1, 2, 3, 4};
        motion.setKey(doc::Channel::Deform, 0, p);
        cel.setMotion(motion);
        QCOMPARE(d.deformPoseAt(cel, 3), p.deform);
    }
};

QTEST_GUILESS_MAIN(TestDeform)
#include "tst_deform.moc"
