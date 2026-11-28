// The rig of 3D models: reading glTF, the pose, skinning, the joints' limits, IK, drawing on the CPU.
#include "core/rig3d/GltfLoader.h"
#include "core/rig3d/Ik.h"
#include "core/rig3d/JointLimit.h"
#include "core/rig3d/Pose.h"
#include "core/rig3d/Skinning.h"
#include "core/rig3d/SoftwareRaster.h"
#include "rig3d_fixture.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <clocale>
#include <cmath>
#include <random>

using namespace mothion::rig3d;

namespace {

constexpr double kDeg = kPi / 180.0;

Model armModel(bool targets = false)
{
    std::string error;
    std::optional<Model> m = loadGltf(rig3d_fixture::armGltf(targets), &error);
    if (!m)
        qFatal("arm model: %s", error.c_str());
    return *m;
}

bool near(const Vec3 &a, const Vec3 &b, double tolerance = 1e-6)
{
    return length(a - b) <= tolerance;
}

// A random turn, uniform over all of them.
Quat randomTurn(std::mt19937 &rng)
{
    std::normal_distribution<double> g;
    return Quat{g(rng), g(rng), g(rng), g(rng)}.normalized();
}

double angleBetween(const Quat &a, const Quat &b)
{
    return 2 * std::acos(std::clamp(std::abs(dot(a, b)), 0.0, 1.0));
}

} // namespace

class TestRig3d : public QObject
{
    Q_OBJECT

private slots:
    void readsGltf();
    void refusesExternalBuffers();
    void readsNumbersWhateverTheLocale();
    void framesWhereTheSkinStands();
    void restSkinsToBind();
    void bendsWithItsBones();
    void morphTargets();
    void poseNumbersRoundTrip();
    void projectionRoundTrip();
    void swingTwistRoundTrip();
    void twistLimitWraps();
    void swingRegionProperties_data();
    void swingRegionProperties();
    void ikReachesTarget();
    void ikKeepsLimits();
    void anchorEndsChain();
    void stiffnessHoldsBack();
    void aimSharesAmongFollowers();
    void twistKeepsLimit();
    void rasterDrawsModel();
};

void TestRig3d::readsGltf()
{
    const Model m = armModel();
    QCOMPARE(m.nodes.size(), std::size_t(5));
    QCOMPARE(m.joints(), (std::vector<int>{0, 1, 2, 3}));
    QCOMPARE(m.nodes[1].parent, 0);
    QCOMPARE(m.nodes[4].skin, 0);
    QCOMPARE(m.order.front(), 0);
    const Primitive &prim = m.meshes[0].primitives[0];
    QCOMPARE(prim.vertexCount(), std::size_t(28));
    QCOMPARE(prim.normals.size(), prim.positions.size()); // made from the faces
    QCOMPARE(prim.weights.offsets.size(), std::size_t(29));
    QCOMPARE(prim.weights.joint.size(), std::size_t(28)); // the zero weights are left out
}

void TestRig3d::refusesExternalBuffers()
{
    const QByteArray json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4,"uri":"arm.bin"}]})";
    std::string error;
    QVERIFY(!loadGltf({json.begin(), json.end()}, &error));
    QVERIFY(error.find("arm.bin") != std::string::npos);
    QVERIFY(!loadGltf({}, &error));
}

// A decimal comma in the locale (Italian, German...) must not cut the file's numbers short.
void TestRig3d::readsNumbersWhateverTheLocale()
{
    const std::string was = std::setlocale(LC_NUMERIC, nullptr);
    bool comma = false;
    for (const char *name : {"it_IT.UTF-8", "it_IT.utf8", "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "Italian_Italy.1252"})
        if (std::setlocale(LC_NUMERIC, name) && std::localeconv()->decimal_point[0] == ',') {
            comma = true;
            break;
        }
    std::vector<std::uint8_t> bytes = rig3d_fixture::armGltf();
    std::string json(bytes.begin(), bytes.end());
    const std::string from = R"("name":"Upper","translation":[0,1,0])";
    json.replace(json.find(from), from.size(), R"("name":"Upper","translation":[0,1.25,0.5])");
    std::string error;
    const std::optional<Model> m = loadGltf({json.begin(), json.end()}, &error);
    std::setlocale(LC_NUMERIC, was.c_str());
    if (!comma)
        QSKIP("No locale with a decimal comma here");
    QVERIFY2(m, error.c_str());
    QCOMPARE(m->nodes[1].translation, (Vec3{0, 1.25, 0.5}));
}

// A node above the bones that moves the skeleton moves the skinned mesh: the camera frames it where it stands, not where
// the skin's bind space is (the Khronos samples turn and scale theirs so).
void TestRig3d::framesWhereTheSkinStands()
{
    std::vector<std::uint8_t> bytes = rig3d_fixture::armGltf();
    std::string json(bytes.begin(), bytes.end());
    const std::string from = R"({"children":[1],"name":"Root"})";
    QVERIFY(json.find(from) != std::string::npos);
    json.replace(json.find(from), from.size(), R"({"children":[1],"name":"Root","translation":[5,0,0]})");
    const std::optional<Model> m = loadGltf({json.begin(), json.end()});
    QVERIFY(m);
    Vec3 center;
    double radius = 0;
    restBounds(*m, center, radius);
    QVERIFY2(near(center, {5, 1.5, 0}, 1e-5), qPrintable(QString("%1 %2 %3").arg(center.x).arg(center.y).arg(center.z)));
    QVERIFY(near(framingCamera(*m).target, {5, 1.5, 0}, 1e-5));
}

void TestRig3d::restSkinsToBind()
{
    const Model m = armModel();
    const auto posed = posedMeshes(m, Pose::rest(m));
    QCOMPARE(posed.size(), std::size_t(1));
    const Primitive &prim = m.meshes[0].primitives[0];
    for (std::size_t i = 0; i < prim.positions.size(); ++i)
        QVERIFY(std::abs(posed[0].positions[i] - prim.positions[i]) < 1e-5);
}

void TestRig3d::bendsWithItsBones()
{
    const Model m = armModel();
    Pose pose = Pose::rest(m);
    pose.rotations[1] = Quat::axisAngle({0, 0, 1}, kPi / 2); // the upper arm turned a quarter, toward −x
    const auto posed = posedMeshes(m, pose);
    // A vertex of the ring at y = 2 (held by bone 2, under bone 1): (0.1, 2, 0.1) goes round (0, 1, 0) to (−1, 1.1, 0.1).
    const std::size_t v = 4 * 4; // ring 4 (y = 2), its first corner
    const Vec3 p{posed[0].positions[v * 3], posed[0].positions[v * 3 + 1], posed[0].positions[v * 3 + 2]};
    QVERIFY2(near(p, {-1, 1.1, 0.1}, 1e-5), qPrintable(QString("%1 %2 %3").arg(p.x).arg(p.y).arg(p.z)));
    // The ring at y = 0.5 (bone 0) does not move.
    const Vec3 q{posed[0].positions[12], posed[0].positions[13], posed[0].positions[14]};
    QVERIFY(near(q, {0.1, 0.5, 0.1}, 1e-6));
    // The normals stay a unit long.
    for (std::size_t i = 0; i + 2 < posed[0].normals.size(); i += 3)
        QVERIFY(std::abs(length({posed[0].normals[i], posed[0].normals[i + 1], posed[0].normals[i + 2]}) - 1) < 1e-4);
}

void TestRig3d::morphTargets()
{
    const Model m = armModel(true);
    QCOMPARE(m.morphs.size(), std::size_t(1));
    QCOMPARE(m.morphs[0].name, std::string("Wide"));
    Pose pose = Pose::rest(m);
    pose.morphs[0] = 0.5;
    const auto posed = posedMeshes(m, pose);
    QVERIFY(std::abs(posed[0].positions[0] - (0.1 + 0.5)) < 1e-5);
}

void TestRig3d::poseNumbersRoundTrip()
{
    const Model m = armModel(true);
    Pose pose = Pose::rest(m);
    pose.rotations[2] = Quat::axisAngle({1, 0, 0}, 0.3);
    pose.translations[0] = {1, 2, 3};
    pose.morphs[0] = 0.25;
    pose.camera.yaw = 30;
    const std::vector<double> n = pose.toNumbers();
    QCOMPARE(n.size(), std::size_t(kCameraNumbers + 5 * kNodeNumbers + 1));
    const Pose back = Pose::fromNumbers(m, n);
    QVERIFY(angleBetween(back.rotations[2], pose.rotations[2]) < 1e-12);
    QCOMPARE(back.translations[0], (Vec3{1, 2, 3}));
    QCOMPARE(back.morphs[0], 0.25);
    QCOMPARE(back.camera.yaw, 30.0);
    // Too few numbers (a key made before the model had more nodes): the rest is as at rest.
    const Pose shorter = Pose::fromNumbers(m, std::vector<double>(n.begin(), n.begin() + kCameraNumbers));
    QVERIFY(angleBetween(shorter.rotations[2], m.nodes[2].rotation) < 1e-12);
}

void TestRig3d::projectionRoundTrip()
{
    const Model m = armModel();
    OrbitCamera camera = framingCamera(m);
    camera.yaw = 25;
    camera.pitch = -10;
    const Projection p = projectionOf(m, camera, 800, 600);
    // The target is in the middle of the picture.
    const Vec3 middle = p.toPage(camera.target);
    QVERIFY(std::abs(middle.x - 400) < 1e-6 && std::abs(middle.y - 300) < 1e-6);
    const Vec3 point{0.3, 2.2, -0.4};
    const Vec3 onPage = p.toPage(point);
    QVERIFY(near(p.fromPage(onPage.x, onPage.y, point), point, 1e-6));
    // Up in the model is up on the page (y down).
    QVERIFY(p.toPage({0, 3, 0}).y < p.toPage({0, 0, 0}).y);
}

void TestRig3d::swingTwistRoundTrip()
{
    std::mt19937 rng(7);
    for (int i = 0; i < 2000; ++i) {
        const Quat q = randomTurn(rng);
        const SwingTwist st = SwingTwist::of(q);
        if (st.direction.x < -0.999)
            continue; // near the pole the swing has no one value
        QVERIFY(angleBetween(st.toQuat(), q) < 1e-6);
    }
    // A bend toward y by 30°: bend 0 is 30°, bend 1 none.
    const SwingTwist bent = SwingTwist::of(Quat::axisAngle({0, 0, 1}, 30 * kDeg));
    QVERIFY(std::abs(bent.bend(0) - 30 * kDeg) < 1e-9);
    QVERIFY(std::abs(bent.bend(1)) < 1e-9);
    QVERIFY(std::abs(bent.twist) < 1e-9);
}

void TestRig3d::twistLimitWraps()
{
    JointSettings s;
    s.limitTwist = true;
    s.twistMin = 150;
    s.twistMax = 210; // across ±180
    const JointLimit limit(s);
    QCOMPARE(limit.clampTwist(170 * kDeg), 170 * kDeg);
    QCOMPARE(limit.clampTwist(-170 * kDeg), -170 * kDeg); // 190°
    QVERIFY(std::abs(limit.clampTwist(100 * kDeg) - 150 * kDeg) < 1e-12); // nearer the low end
    QVERIFY(std::abs(limit.clampTwist(-100 * kDeg) - (-150 * kDeg)) < 1e-12); // 260°: nearer 210°
    s.twistMin = s.twistMax = 20; // locked
    QVERIFY(std::abs(JointLimit(s).clampTwist(1.0) - 20 * kDeg) < 1e-12);
}

void TestRig3d::swingRegionProperties_data()
{
    QTest::addColumn<bool>("rectangle");
    QTest::addColumn<double>("aMin");
    QTest::addColumn<double>("aMax");
    QTest::addColumn<double>("bMin");
    QTest::addColumn<double>("bMax");
    QTest::addColumn<bool>("bLimited");
    QTest::newRow("rectangle") << true << -30.0 << 60.0 << -45.0 << 20.0 << true;
    QTest::newRow("stadium wide") << false << -20.0 << 20.0 << -80.0 << 70.0 << true;
    QTest::newRow("stadium tall") << false << -90.0 << 100.0 << -15.0 << 25.0 << true;
    QTest::newRow("one bend free") << false << -10.0 << 40.0 << 0.0 << 0.0 << false;
    QTest::newRow("hinge") << true << -5.0 << 120.0 << 0.0 << 0.0 << true; // the second bend locked
    QTest::newRow("off center") << false << 30.0 << 110.0 << 10.0 << 50.0 << true;
}

// What the research's prototype checked (points in stay, points out come in, twice is once), and that the point a
// swing is clamped to is the nearest of the region: no point of it, sampled densely, is nearer.
void TestRig3d::swingRegionProperties()
{
    QFETCH(bool, rectangle);
    QFETCH(double, aMin);
    QFETCH(double, aMax);
    QFETCH(double, bMin);
    QFETCH(double, bMax);
    QFETCH(bool, bLimited);
    JointSettings s;
    s.rectangle = rectangle;
    s.limitBend[0] = true;
    s.bendMin[0] = aMin;
    s.bendMax[0] = aMax;
    s.limitBend[1] = bLimited;
    s.bendMin[1] = bMin;
    s.bendMax[1] = bMax;
    const JointLimit limit(s);
    // The region sampled densely, over the two bends (a locked one too, which has no area on the sphere).
    std::mt19937 rng(11);
    std::normal_distribution<double> g;
    std::vector<Vec3> region;
    const double b0 = bLimited ? bMin : -179.9, b1 = bLimited ? bMax : 179.9;
    for (int i = 0; i <= 400; ++i)
        for (int k = 0; k <= 400; ++k) {
            const double y = std::tan((aMin + (aMax - aMin) * i / 400.0) * kDeg / 2);
            const double z = std::tan((b0 + (b1 - b0) * k / 400.0) * kDeg / 2);
            const double r2 = y * y + z * z;
            const Vec3 v{(1 - r2) / (1 + r2), 2 * y / (1 + r2), 2 * z / (1 + r2)};
            if (limit.swingContains(v, 1e-9))
                region.push_back(v);
        }
    QVERIFY(region.size() > 100);
    int outside = 0;
    for (int i = 0; i < 3000; ++i) {
        const Vec3 v = normalized({g(rng), g(rng), g(rng)});
        if (v.x < -0.995)
            continue; // round the pole behind the bone the region's image is at infinity
        const Vec3 c = limit.clampSwing(v);
        QVERIFY(std::abs(length(c) - 1) < 1e-9);
        QVERIFY(limit.swingContains(c, 1e-7));
        QVERIFY(near(limit.clampSwing(c), c, 1e-7)); // idempotent
        if (limit.swingContains(v)) {
            QVERIFY(c == v); // in: not touched
            continue;
        }
        ++outside;
        double best = -2;
        for (const Vec3 &r : region)
            best = std::max(best, dot(r, v));
        QVERIFY2(dot(c, v) >= best - 1e-5, qPrintable(QString("not the nearest: %1 < %2").arg(dot(c, v)).arg(best)));
    }
    QVERIFY(outside > 100);
}

void TestRig3d::ikReachesTarget()
{
    const Model m = armModel();
    const Rig rig = Rig::make(m, {});
    QCOMPARE(ikChain(m, rig, 3), (std::vector<int>{2, 1, 0}));
    Pose pose = Pose::rest(m);
    const Vec3 target{1.2, 1.5, 0.6}; // within reach (3 from the root)
    QVERIFY(solveIk(m, rig, pose, 3, target, {64, 1e-4}));
    QVERIFY(length(nodePosition(m, pose, 3) - target) < 1e-3);
    // The bones keep their lengths.
    QVERIFY(std::abs(length(nodePosition(m, pose, 2) - nodePosition(m, pose, 1)) - 1) < 1e-9);
}

void TestRig3d::ikKeepsLimits()
{
    const Model m = armModel();
    std::vector<JointSettings> settings(m.nodes.size());
    for (int j : {0, 1, 2}) {
        settings[std::size_t(j)].limitBend[0] = settings[std::size_t(j)].limitBend[1] = true;
        settings[std::size_t(j)].bendMin[0] = -40;
        settings[std::size_t(j)].bendMax[0] = 40;
        settings[std::size_t(j)].bendMin[1] = -20;
        settings[std::size_t(j)].bendMax[1] = 20;
        settings[std::size_t(j)].limitTwist = true;
        settings[std::size_t(j)].twistMin = settings[std::size_t(j)].twistMax = 0;
        settings[std::size_t(j)].softZone = 10;
    }
    const Rig rig = Rig::make(m, settings);
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(-3, 3);
    for (int trial = 0; trial < 30; ++trial) {
        Pose pose = Pose::rest(m);
        solveIk(m, rig, pose, 3, {u(rng), u(rng), u(rng)});
        for (int j : {0, 1, 2}) {
            const Quat q = pose.rotations[std::size_t(j)];
            QVERIFY(angleBetween(rig.limits[std::size_t(j)].clamp(q), q) < 1e-6); // within its limits
        }
    }
}

void TestRig3d::anchorEndsChain()
{
    const Model m = armModel();
    std::vector<JointSettings> settings(m.nodes.size());
    settings[1].anchor = true;
    const Rig rig = Rig::make(m, settings);
    QCOMPARE(ikChain(m, rig, 3), (std::vector<int>{2}));
    Pose pose = Pose::rest(m);
    solveIk(m, rig, pose, 3, {2, 2, 0});
    QVERIFY(angleBetween(pose.rotations[1], m.nodes[1].rotation) < 1e-12); // the anchor did not turn
    QVERIFY(angleBetween(pose.rotations[0], m.nodes[0].rotation) < 1e-12); // nor what it hangs from
}

void TestRig3d::stiffnessHoldsBack()
{
    const Model m = armModel();
    std::vector<JointSettings> settings(m.nodes.size());
    settings[2].stiffness = 0.9;
    const Rig rig = Rig::make(m, settings);
    Pose pose = Pose::rest(m);
    IkOptions once;
    once.iterations = 1;
    solveIk(m, rig, pose, 3, {1, 3, 0}, once);
    // The stiff bone turned a tenth of what a free one would (45°: from (0, 1) toward (1, 1), seen from its start).
    const double turned = angleBetween(pose.rotations[2], m.nodes[2].rotation);
    QVERIFY(std::abs(turned - 4.5 * kDeg) < 1e-6);
}

void TestRig3d::aimSharesAmongFollowers()
{
    const Model m = armModel();
    std::vector<JointSettings> settings(m.nodes.size());
    settings[1].follow = true;
    const Rig rig = Rig::make(m, settings);
    Pose pose = Pose::rest(m);
    const Vec3 target{1.5, 2, 0};
    aimAt(m, rig, pose, 3, target);
    // The node points at the target from its parent.
    const Vec3 from = nodePosition(m, pose, 2), tip = nodePosition(m, pose, 3);
    QVERIFY(length(cross(normalized(tip - from), normalized(target - from))) < 1e-6);
    // Bone 1 (a follower) turned; bone 0 (no follower) did not.
    QVERIFY(angleBetween(pose.rotations[1], m.nodes[1].rotation) > 1e-3);
    QVERIFY(angleBetween(pose.rotations[0], m.nodes[0].rotation) < 1e-12);
    // Without followers only the parent turns.
    Pose plain = Pose::rest(m);
    aimAt(m, Rig::make(m, {}), plain, 3, target);
    QVERIFY(angleBetween(plain.rotations[1], m.nodes[1].rotation) < 1e-12);
}

void TestRig3d::twistKeepsLimit()
{
    const Model m = armModel();
    std::vector<JointSettings> settings(m.nodes.size());
    settings[1].limitTwist = true;
    settings[1].twistMin = -10;
    settings[1].twistMax = 10;
    const Rig rig = Rig::make(m, settings);
    Pose pose = Pose::rest(m);
    for (int i = 0; i < 30; ++i)
        twistNode(m, rig, pose, 1, kDeg);
    QVERIFY(std::abs(angleBetween(pose.rotations[1], m.nodes[1].rotation) - 10 * kDeg) < 1e-6);
    // The bone still points where it did: a twist does not swing it.
    QVERIFY(near(nodePosition(m, pose, 2), {0, 2, 0}, 1e-9));
}

void TestRig3d::rasterDrawsModel()
{
    const Model m = armModel();
    const Pose pose = Pose::rest(m);
    const Projection p = projectionOf(m, pose.camera, 64, 64);
    std::vector<std::uint32_t> pixels;
    rasterize(m, posedMeshes(m, pose), p, pose.camera.eye(), {}, 64, 64, pixels);
    QCOMPARE(pixels.size(), std::size_t(64 * 64));
    QVERIFY((pixels[32 * 64 + 32] >> 24) == 255); // the arm is in the middle
    QCOMPARE(pixels[32 * 64 + 2] >> 24, 0u);       // and not at the side
}

QTEST_GUILESS_MAIN(TestRig3d)
#include "tst_rig3d.moc"
