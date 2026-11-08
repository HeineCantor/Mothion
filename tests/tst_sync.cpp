// Tests for shared sessions: a host and one or two others, over lines inside the process and over TCP.

#include "core/document/commands/LayerCommands.h"
#include "document/DocumentController.h"
#include "sync/SyncClient.h"
#include "sync/SyncHost.h"
#include "sync/Transport.h"

#include <QSignalSpy>
#include <QtTest>

using namespace mothion;

namespace {

// The same document, part by part (what a session promises its people).
bool sameDocument(const doc::Document &x, const doc::Document &y, QString *why = nullptr)
{
    auto no = [&](const QString &text) {
        if (why)
            *why = text;
        return false;
    };
    if (x.width() != y.width() || x.height() != y.height())
        return no(QStringLiteral("size"));
    if (x.frameCount() != y.frameCount() || x.paperColor() != y.paperColor() || !(x.frameRate() == y.frameRate()))
        return no(QStringLiteral("global"));
    if (x.celCount() != y.celCount())
        return no(QStringLiteral("cel count %1 / %2").arg(x.celCount()).arg(y.celCount()));
    for (int i = 0; i < x.celCount(); ++i) {
        const doc::Cel &a = x.celAt(i), &b = y.celAt(i);
        if (a.id() != b.id() || !(a.properties() == b.properties()) || !(a.timing() == b.timing()))
            return no(QStringLiteral("cel %1").arg(i));
        if (a.drawings().size() != b.drawings().size())
            return no(QStringLiteral("drawings of cel %1").arg(i));
        for (const auto &[id, da] : a.drawings()) {
            const doc::Drawing *db = b.drawing(id);
            if (!db || da.name != db->name || da.layers.size() != db->layers.size())
                return no(QStringLiteral("drawing %1").arg(id));
            for (std::size_t k = 0; k < da.layers.size(); ++k) {
                const doc::Layer &la = da.layers[k], &lb = db->layers[k];
                if (la.id != lb.id || la.kind != lb.kind || !(la.properties == lb.properties)
                    || la.mask.has_value() != lb.mask.has_value())
                    return no(QStringLiteral("layer %1").arg(la.id));
                auto same = [](const doc::TiledImage &p, const doc::TiledImage &q) {
                    auto cp = p.tileCoords(), cq = q.tileCoords();
                    std::sort(cp.begin(), cp.end());
                    std::sort(cq.begin(), cq.end());
                    if (cp != cq)
                        return false;
                    for (const doc::TileCoord &c : cp)
                        if (p.tile(c)->pixels != q.tile(c)->pixels)
                            return false;
                    return true;
                };
                if (!same(la.image, lb.image) || (la.mask && !same(*la.mask, *lb.mask)))
                    return no(QStringLiteral("pixels of layer %1").arg(la.id));
            }
        }
    }
    return true;
}

// One dot on the layer being drawn on, as a stroke is recorded.
bool paintDot(document::DocumentController &c, int x, int y, doc::Pixel color, QString *reason = nullptr)
{
    const auto target = c.beginPaint(true, reason);
    if (!target)
        return false;
    const doc::TiledImage before = *target->image;
    target->image->setPixel(x, y, color);
    c.notifyPainted(QRect(x, y, 1, 1));
    c.endPaint(before);
    return true;
}

doc::Pixel pixelOf(const document::DocumentController &c, doc::CelId cel, int x, int y)
{
    const doc::Cel *shown = c.document().cel(cel);
    const doc::Drawing *drawing = shown ? shown->drawingAt(0) : nullptr;
    return drawing && !drawing->layers.empty() ? drawing->layers.front().image.pixel(x, y) : 0;
}

} // namespace

class TestSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("MothionTest"));
        QCoreApplication::setApplicationName(QStringLiteral("MothionTest"));
    }

    // The one who joins gets the host's project, whole.
    void joiningCopiesTheProject()
    {
        document::DocumentController hostDoc, guestDoc;
        QImage red(100, 80, QImage::Format_ARGB32);
        red.fill(Qt::red);
        hostDoc.importImage(red, QStringLiteral("x"));
        hostDoc.addCel();
        sync::SyncHost host(&hostDoc, QStringLiteral("Host"));
        sync::SyncClient guest(&guestDoc, QStringLiteral("Anna"));
        auto [a, b] = sync::MemoryTransport::makePair();
        QSignalSpy joined(&guest, &sync::SyncClient::joined);
        host.addTransport(a);
        guest.attach(b, QString());
        QVERIFY(joined.wait(5000));
        QString why;
        QVERIFY2(sameDocument(hostDoc.document(), guestDoc.document(), &why), qPrintable(why));
        QVERIFY(guest.isJoined());
        QCOMPARE(host.participantNames(), (QStringList{QStringLiteral("Host"), QStringLiteral("Anna")}));
    }

    // A stroke by the guest ends up in the host's document, and an undo of it too.
    void aGuestsStrokeReachesTheHostAndSoDoesItsUndo()
    {
        document::DocumentController hostDoc, guestDoc;
        QImage red(100, 80, QImage::Format_ARGB32);
        red.fill(Qt::red);
        hostDoc.importImage(red, QStringLiteral("x"));
        const doc::CelId cel = hostDoc.currentCel();
        hostDoc.addCel(); // the host is on another cel: the frame is free for the guest
        sync::SyncHost host(&hostDoc, QStringLiteral("Host"));
        sync::SyncClient guest(&guestDoc, QStringLiteral("Anna"));
        auto [a, b] = sync::MemoryTransport::makePair();
        QSignalSpy joined(&guest, &sync::SyncClient::joined);
        host.addTransport(a);
        guest.attach(b, QString());
        QVERIFY(joined.wait(5000));
        guestDoc.setCurrentCel(cel);

        QVERIFY(paintDot(guestDoc, 5, 5, 0xff000000));
        QTRY_COMPARE_WITH_TIMEOUT(pixelOf(hostDoc, cel, 5, 5), doc::Pixel(0xff000000), 5000);
        QString why;
        QVERIFY2(sameDocument(hostDoc.document(), guestDoc.document(), &why), qPrintable(why));

        guestDoc.undo();
        QTRY_COMPARE_WITH_TIMEOUT(pixelOf(hostDoc, cel, 5, 5), pixelOf(guestDoc, cel, 5, 5), 5000);
        QVERIFY(pixelOf(hostDoc, cel, 5, 5) != doc::Pixel(0xff000000));
        QVERIFY2(sameDocument(hostDoc.document(), guestDoc.document(), &why), qPrintable(why));
    }

    // What the host does goes to the guest: a cel, a layer's look, a stroke.
    void theHostsChangesReachTheGuest()
    {
        document::DocumentController hostDoc, guestDoc;
        QImage red(100, 80, QImage::Format_ARGB32);
        red.fill(Qt::red);
        hostDoc.importImage(red, QStringLiteral("x"));
        sync::SyncHost host(&hostDoc, QStringLiteral("Host"));
        sync::SyncClient guest(&guestDoc, QStringLiteral("Anna"));
        auto [a, b] = sync::MemoryTransport::makePair();
        QSignalSpy joined(&guest, &sync::SyncClient::joined);
        host.addTransport(a);
        guest.attach(b, QString());
        QVERIFY(joined.wait(5000));

        hostDoc.addCel();
        const doc::CelId made = hostDoc.currentCel();
        QVERIFY(made % sync::SyncHost::kMaxParticipants == 0); // the host's ids are its own
        QTRY_VERIFY_WITH_TIMEOUT(guestDoc.document().cel(made), 5000);

        QVERIFY(paintDot(hostDoc, 7, 7, 0xff00ff00));
        const doc::CelId top = hostDoc.currentCel();
        QString why;
        QTRY_VERIFY_WITH_TIMEOUT(sameDocument(hostDoc.document(), guestDoc.document(), &why), 5000);

        const doc::Drawing *drawing = hostDoc.currentDrawing();
        doc::LayerProperties props = drawing->layers.front().properties;
        props.visible = false;
        props.opacity = 0.5f;
        hostDoc.execute(doc::makeSetLayerProperties(hostDoc.document(), top, drawing->id, drawing->layers.front().id, props));
        QTRY_VERIFY_WITH_TIMEOUT(sameDocument(hostDoc.document(), guestDoc.document(), &why), 5000);
        const doc::Layer *there = guestDoc.document().layer(top, drawing->id, drawing->layers.front().id);
        QVERIFY(there && !there->properties.visible);
    }

    // Both make cels at once: the ids do not clash.
    void idsFromTwoPeopleDoNotClash()
    {
        document::DocumentController hostDoc, guestDoc;
        sync::SyncHost host(&hostDoc, QStringLiteral("Host"));
        sync::SyncClient guest(&guestDoc, QStringLiteral("Anna"));
        auto [a, b] = sync::MemoryTransport::makePair();
        QSignalSpy joined(&guest, &sync::SyncClient::joined);
        host.addTransport(a);
        guest.attach(b, QString());
        QVERIFY(joined.wait(5000));

        hostDoc.addCel();
        guestDoc.addCel();
        const doc::CelId fromHost = hostDoc.currentCel(), fromGuest = guestDoc.currentCel();
        QVERIFY(fromHost != fromGuest);
        QVERIFY(fromHost % sync::SyncHost::kMaxParticipants == 0);
        QVERIFY(fromGuest % sync::SyncHost::kMaxParticipants == 1);
        QString why;
        QTRY_VERIFY_WITH_TIMEOUT(hostDoc.document().cel(fromGuest) && guestDoc.document().cel(fromHost), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(hostDoc.document().celCount() == guestDoc.document().celCount(), 5000);
        QCOMPARE(hostDoc.document().celCount(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(sameDocument(hostDoc.document(), guestDoc.document(), &why), 5000);
    }

    // Whoever is on a frame holds it: the other cannot draw on it, until the first goes elsewhere.
    void aFrameHeldByOneIsRefusedToTheOther()
    {
        document::DocumentController hostDoc, guestDoc;
        QImage red(100, 80, QImage::Format_ARGB32);
        red.fill(Qt::red);
        hostDoc.importImage(red, QStringLiteral("x"));
        const doc::CelId cel = hostDoc.currentCel();
        hostDoc.addCel();
        const doc::CelId other = hostDoc.currentCel();
        hostDoc.setCurrentCel(cel); // the host is on the frame the guest will want
        sync::SyncHost host(&hostDoc, QStringLiteral("Host"));
        sync::SyncClient guest(&guestDoc, QStringLiteral("Anna"));
        auto [a, b] = sync::MemoryTransport::makePair();
        QSignalSpy joined(&guest, &sync::SyncClient::joined);
        host.addTransport(a);
        guest.attach(b, QString());
        QVERIFY(joined.wait(5000));
        guestDoc.setCurrentCel(cel);

        QTRY_VERIFY_WITH_TIMEOUT(!guest.lockOwner(qint64(cel), qint64(hostDoc.currentDrawing()->id)).isEmpty(), 5000);
        QString reason;
        QVERIFY(!paintDot(guestDoc, 3, 3, 0xff000000, &reason));
        QVERIFY2(reason.contains(QStringLiteral("Host")), qPrintable(reason));
        QVERIFY(guestDoc.drawingBlocked()); // for the cursor

        hostDoc.setCurrentCel(other); // the host leaves the frame
        QTRY_VERIFY_WITH_TIMEOUT(guest.lockOwner(qint64(cel), qint64(guestDoc.currentDrawing()->id)) != QStringLiteral("Host"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(paintDot(guestDoc, 3, 3, 0xff000000), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(pixelOf(hostDoc, cel, 3, 3), doc::Pixel(0xff000000), 5000);
    }

    // A change to a frame held by someone else that gets to the host anyway (the guest's guard is off) is refused, and
    // the guest's copy goes back to the host's.
    void aRefusedChangeIsPutRight()
    {
        document::DocumentController hostDoc, guestDoc;
        QImage red(100, 80, QImage::Format_ARGB32);
        red.fill(Qt::red);
        hostDoc.importImage(red, QStringLiteral("x"));
        const doc::CelId cel = hostDoc.currentCel();
        sync::SyncHost host(&hostDoc, QStringLiteral("Host"));
        sync::SyncClient guest(&guestDoc, QStringLiteral("Anna"));
        auto [a, b] = sync::MemoryTransport::makePair();
        QSignalSpy joined(&guest, &sync::SyncClient::joined);
        QSignalSpy noticed(&host, &sync::SyncHost::notice);
        QSignalSpy told(&guest, &sync::SyncClient::notice);
        host.addTransport(a);
        guest.attach(b, QString());
        QVERIFY(joined.wait(5000));
        guestDoc.setCurrentCel(cel);
        QTRY_VERIFY_WITH_TIMEOUT(!guest.lockOwner(qint64(cel), qint64(hostDoc.currentDrawing()->id)).isEmpty(), 5000);

        guestDoc.setEditGuard({}); // nothing stops the guest from trying
        QVERIFY(paintDot(guestDoc, 9, 9, 0xff0000ff));
        QTRY_VERIFY_WITH_TIMEOUT(!told.isEmpty(), 5000);
        QCOMPARE(pixelOf(hostDoc, cel, 9, 9), pixelOf(hostDoc, cel, 90, 70)); // the host did not take it (or the image)
        QString why;
        QTRY_VERIFY_WITH_TIMEOUT(sameDocument(hostDoc.document(), guestDoc.document(), &why), 5000);
        QVERIFY(!noticed.isEmpty());
    }

    // A layer's mask and a layer made by the guest cross as well.
    void layersAndMasksCross()
    {
        document::DocumentController hostDoc, guestDoc;
        QImage red(100, 80, QImage::Format_ARGB32);
        red.fill(Qt::red);
        hostDoc.importImage(red, QStringLiteral("x"));
        const doc::CelId cel = hostDoc.currentCel();
        hostDoc.addCel();
        sync::SyncHost host(&hostDoc, QStringLiteral("Host"));
        sync::SyncClient guest(&guestDoc, QStringLiteral("Anna"));
        auto [a, b] = sync::MemoryTransport::makePair();
        QSignalSpy joined(&guest, &sync::SyncClient::joined);
        host.addTransport(a);
        guest.attach(b, QString());
        QVERIFY(joined.wait(5000));
        guestDoc.setCurrentCel(cel);

        QVERIFY(guestDoc.addLayer(true));
        QVERIFY(guestDoc.createLayerMask());
        QVERIFY(paintDot(guestDoc, 4, 4, 0xff000000)); // on the mask
        QString why;
        QTRY_VERIFY_WITH_TIMEOUT(sameDocument(hostDoc.document(), guestDoc.document(), &why), 5000);
        bool masked = false;
        for (const doc::Layer &layer : hostDoc.document().cel(cel)->drawingAt(0)->layers)
            masked = masked || (layer.mask && layer.mask->pixel(4, 4) == doc::Pixel(0xff000000));
        QVERIFY(masked);
    }

    // Over TCP: with the right code a guest gets in and draws; with the wrong one it does not.
    void tcpSessionWithAnInvitationCode()
    {
        document::DocumentController hostDoc, guestDoc, intruderDoc;
        QImage red(100, 80, QImage::Format_ARGB32);
        red.fill(Qt::red);
        hostDoc.importImage(red, QStringLiteral("x"));
        const doc::CelId cel = hostDoc.currentCel();
        hostDoc.addCel();
        sync::SyncHost host(&hostDoc, QStringLiteral("Host"));
        QString error;
        QVERIFY2(host.listen(0, QStringLiteral("sesame"), &error), qPrintable(error));
        QVERIFY(host.port() != 0);

        sync::SyncClient intruder(&intruderDoc, QStringLiteral("Mallory"));
        QSignalSpy refused(&intruder, &sync::SyncClient::failed);
        intruder.connectTo(QStringLiteral("127.0.0.1"), host.port(), QStringLiteral("wrong"));
        QVERIFY(refused.wait(5000));
        QVERIFY2(refused.first().first().toString().contains(QStringLiteral("code")), qPrintable(refused.first().first().toString()));
        QVERIFY(!intruder.isJoined());

        sync::SyncClient guest(&guestDoc, QStringLiteral("Anna"));
        QSignalSpy joined(&guest, &sync::SyncClient::joined);
        guest.connectTo(QStringLiteral("127.0.0.1"), host.port(), QStringLiteral("sesame"));
        QVERIFY(joined.wait(10000));
        guestDoc.setCurrentCel(cel);
        QVERIFY(paintDot(guestDoc, 6, 6, 0xff123456));
        QTRY_COMPARE_WITH_TIMEOUT(pixelOf(hostDoc, cel, 6, 6), doc::Pixel(0xff123456), 5000);

        // When the guest leaves the host goes on alone, and the guest keeps a project of its own.
        QSignalSpy gone(&host, &sync::SyncHost::participantsChanged);
        guest.leave();
        QVERIFY(!guest.isJoined());
        QTRY_VERIFY_WITH_TIMEOUT(host.participantNames().size() == 1, 5000);
        QVERIFY(guestDoc.document().celCount() >= 1);
    }
};

QTEST_MAIN(TestSync)
#include "tst_sync.moc"
