#include "canvas/RasterSurface.h"
#include "canvas/StrokeRenderer.h"
#include "canvas/ViewTransform.h"

#include <QTest>

using namespace mothion::canvas;

namespace {

ViewTransform makeView()
{
    ViewTransform v;
    v.setDocumentSize({1920, 1080});
    v.setViewportSize({800, 600});
    v.fitToView();
    return v;
}

bool near(const QPointF &a, const QPointF &b) { return QLineF(a, b).length() < 1e-6; }

} // namespace

class TestCanvas : public QObject
{
    Q_OBJECT

private slots:
    void fitCentersDocument()
    {
        const ViewTransform v = makeView();
        QVERIFY(near(v.documentToView().map(QPointF(960, 540)), QPointF(400, 300)));
    }

    void zoomKeepsAnchorFixed()
    {
        ViewTransform v = makeView();
        const QPointF anchor(123, 456);
        const QPointF doc = v.mapToDocument(anchor);
        v.zoomAround(anchor, 3.0);
        QVERIFY(near(v.documentToView().map(doc), anchor));
    }

    void rotateKeepsAnchorFixedAndWraps()
    {
        ViewTransform v = makeView();
        const QPointF anchor(600, 100);
        const QPointF doc = v.mapToDocument(anchor);
        v.rotateAround(anchor, 170);
        v.rotateAround(anchor, 30);
        QVERIFY(near(v.documentToView().map(doc), anchor));
        QCOMPARE(qRound(v.rotation()), -160);
    }

    void viewToDocumentRoundTrip()
    {
        ViewTransform v = makeView();
        v.rotateAround({10, 20}, 33);
        v.zoomAround({300, 200}, 1.7);
        v.panBy({15, -40});
        const QPointF p(250.5, 120.25);
        QVERIFY(near(v.documentToView().map(v.mapToDocument(p)), p));
    }

    void strokeLeavesNoGaps()
    {
        RasterSurface surface({200, 50});
        StrokeRenderer renderer;
        surface.beginStroke(Qt::black, 1.0, false);
        renderer.begin({{10, 25}, 2.0}, surface);
        // One long jump: the renderer must fill it with dabs.
        renderer.continueTo({{190, 25}, 2.0}, surface);
        surface.endStroke();
        for (int x = 10; x <= 190; ++x)
            QVERIFY2(qAlpha(surface.image().pixel(x, 25)) > 200, qPrintable(QString::number(x)));
    }

    void strokeOpacityDoesNotAccumulate()
    {
        RasterSurface surface({100, 20});
        StrokeRenderer renderer;
        surface.beginStroke(Qt::black, 0.5, false);
        renderer.begin({{10, 10}, 4.0}, surface);
        renderer.continueTo({{90, 10}, 4.0}, surface); // many overlapping dabs
        surface.endStroke();
        const int alpha = qAlpha(surface.image().pixel(50, 10));
        QVERIFY2(qAbs(alpha - 128) <= 2, qPrintable(QString::number(alpha)));
    }

    void eraserRemovesPaint()
    {
        RasterSurface surface({100, 20});
        StrokeRenderer renderer;
        surface.beginStroke(Qt::black, 1.0, false);
        renderer.begin({{10, 10}, 4.0}, surface);
        renderer.continueTo({{90, 10}, 4.0}, surface);
        surface.endStroke();
        surface.beginStroke(Qt::black, 1.0, true);
        renderer.begin({{50, 0}, 4.0}, surface);
        renderer.continueTo({{50, 19}, 4.0}, surface);
        surface.endStroke();
        QCOMPARE(qAlpha(surface.image().pixel(50, 10)), 0);
        QVERIFY(qAlpha(surface.image().pixel(20, 10)) > 200);
    }
};

QTEST_MAIN(TestCanvas)
#include "tst_canvas.moc"
