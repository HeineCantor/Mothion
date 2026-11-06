// Tests for the UI layer: the compositing window and the node editor.

#include "test_common.h"

class TestUiCompositing : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_settingsDir;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // keep the user's brushes out
        // And the user's settings (recent projects...): a folder of their own.
        QCoreApplication::setOrganizationName(QStringLiteral("MothionTest"));
        QCoreApplication::setApplicationName(QStringLiteral("MothionTest"));
        QVERIFY(m_settingsDir.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }


    // The track's scrubbing can be turned off: a step or a click on the timeline is then silent; the setting is one
    // undo step and goes into the project.
    // The example network: Frame > Blur > Merge (with a Color in front) > Output.
    static void exampleNetwork(compositing::Graph &g, compositing::NodeId ids[5])
    {
        ids[0] = g.addNode(QStringLiteral("frame"), {-330, -70});
        ids[1] = g.addNode(QStringLiteral("blur"), {-90, -110});
        ids[2] = g.addNode(QStringLiteral("merge"), {160, -40});
        ids[3] = g.addNode(QStringLiteral("color"), {-330, 60});
        ids[4] = g.addNode(QStringLiteral("output"), {400, -30});
        g.connect(ids[0], 0, ids[1], 0);
        g.connect(ids[1], 0, ids[2], 0);
        g.connect(ids[3], 0, ids[2], 1);
        g.connect(ids[2], 0, ids[4], 0);
    }


    void compositingEditsAreKeptInTheDocumentAndUndone()
    {
        document::DocumentController controller;
        ui::CompositingWindow window(&controller);
        QVERIFY(controller.compositing().nodes.empty()); // nothing is stored until the user edits
        QCOMPARE(window.network().nodes().size(), size_t(2)); // the starting network: Frame to Output
        // A node added, then a slider moved: kept as one step each, after the gesture.
        const compositing::NodeId blur = window.network().addNode(QStringLiteral("blur"), {0, 100});
        QTRY_VERIFY(!controller.compositing().nodes.empty()); // the idle timer commits
        QCOMPARE(controller.document().compositing().nodes.size(), size_t(3));
        window.network().setParam(blur, QStringLiteral("radius"), 3);
        window.network().setParam(blur, QStringLiteral("radius"), 4);
        window.network().setParam(blur, QStringLiteral("radius"), 5);
        auto storedRadius = [&controller] {
            int radius = 0;
            for (const auto &node : controller.compositing().nodes)
                for (const auto &[key, value] : node.params)
                    radius = (node.type == "blur" && key == "radius") ? value : radius;
            return radius;
        };
        QTRY_COMPARE(storedRadius(), 5);
        // Undo: the slider goes back, the node stays.
        controller.undo();
        QCOMPARE(storedRadius(), 8);
        QCOMPARE(window.network().node(blur)->params.value(QStringLiteral("radius")).toInt(), 8);
        controller.redo();
        QCOMPARE(window.network().node(blur)->params.value(QStringLiteral("radius")).toInt(), 5);
        QCOMPARE(controller.compositing().nodes.size(), size_t(3));
        // Undo twice: back to nothing stored, and the window shows its starting network again.
        controller.undo();
        controller.undo();
        QVERIFY(controller.compositing().nodes.empty());
        QCOMPARE(window.network().nodes().size(), size_t(2));
    }


    void compositingPlaysTheSceneAndSkipsWhatIsLate()
    {
        document::DocumentController controller;
        ui::CompositingWindow window(&controller);
        window.show();
        const int start = controller.currentFrame();
        window.play();
        QVERIFY(window.isPlaying());
        QTRY_VERIFY(window.framesShown() >= 3);
        QVERIFY(controller.currentFrame() != start); // the timeline follows
        QVERIFY(window.preview()->image().width() <= 640); // a smaller picture while playing
        window.stop();
        QVERIFY(!window.isPlaying());
        QVERIFY(window.preview()->image().width() > 640 || controller.document().width() <= 640);
        const int where = controller.currentFrame();
        QTest::qWait(150);
        QCOMPARE(controller.currentFrame(), where); // stopped means stopped
        // Hiding the window stops it too.
        window.play();
        window.hide();
        QVERIFY(!window.isPlaying());
    }


    void playBarScrubsAndRangeLimitsThePlayback()
    {
        document::DocumentController controller;
        ui::CompositingWindow window(&controller);
        window.show();
        const int last = controller.document().frameCount() - 1;
        QCOMPARE(window.playbar()->maximum(), last);
        QCOMPARE(window.rangeBar()->first(), 0);
        QCOMPARE(window.rangeBar()->last(), last); // the whole scene, until narrowed
        // The play bar moves the current frame, and follows it.
        window.playbar()->setValue(7);
        QCOMPARE(controller.currentFrame(), 7);
        controller.setCurrentFrame(9);
        QCOMPARE(window.playbar()->value(), 9);
        // Dragging a handle of the range narrows it.
        ui::RangeSlider *range = window.rangeBar();
        range->resize(400, 26);
        QTest::mousePress(range, Qt::LeftButton, {}, QPoint(12 + int(376.0 * 10 / last), 13));
        QTest::mouseMove(range, QPoint(12 + int(376.0 * 12 / last), 13));
        QTest::mouseRelease(range, Qt::LeftButton, {}, QPoint(12 + int(376.0 * 12 / last), 13));
        QVERIFY(range->first() >= 11 && range->first() <= 13);
        range->setRange(10, 13);
        // Playing stays inside it, and starts from its beginning when the frame is outside.
        controller.setCurrentFrame(2);
        window.play();
        QTRY_VERIFY(window.framesShown() >= 3);
        QVERIFY(controller.currentFrame() >= 10 && controller.currentFrame() <= 13);
        window.stop();
        // An untouched range follows the scene's length (another project, here).
        range->setRange(0, last);
        auto longer = std::make_unique<doc::Document>(64, 48);
        longer->setFrameCount(last + 11);
        controller.replaceDocument(std::move(longer));
        QCOMPARE(window.rangeBar()->last(), last + 10);
    }


    void nodeViewIsKeptInTheDocument()
    {
        document::DocumentController controller;
        {
            ui::CompositingWindow window(&controller);
            window.show();
            QTRY_VERIFY(window.graph()->viewZoom() > 0);
            QVERIFY(!controller.compositingView().valid); // not touched: nothing to keep
            window.graph()->zoomBy(1.5);
            window.graph()->horizontalScrollBar()->setValue(window.graph()->horizontalScrollBar()->value() + 40);
            QTRY_VERIFY(controller.compositingView().valid);
        } // closing keeps the last view
        const doc::GraphView kept = controller.compositingView();
        ui::CompositingWindow again(&controller);
        again.show();
        QTRY_VERIFY(qAbs(again.graph()->viewZoom() - kept.zoom) < 1e-6);
        QVERIFY(QLineF(again.graph()->viewCenter(), QPointF(kept.x, kept.y)).length() < 2);
    }


    void compositingPreviewZoomsAndMoves()
    {
        ui::ImagePreview preview;
        preview.resize(400, 300);
        preview.show();
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        preview.setImage(image);
        QCOMPARE(preview.zoom(), 1.0); // fitted at first
        const QPointF fittedCenter = preview.imagePointAt(QPointF(200, 150));
        QCOMPARE(fittedCenter, QPointF(100, 50));

        // The wheel zooms around the pointer: the point of the image under it stays there.
        const QPoint pointer(300, 120);
        const QPointF before = preview.imagePointAt(pointer);
        QWheelEvent zoomIn(pointer, preview.mapToGlobal(pointer), {}, QPoint(0, 480), Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
        QApplication::sendEvent(&preview, &zoomIn);
        QVERIFY(preview.zoom() > 1.5);
        QVERIFY(QLineF(preview.imagePointAt(pointer), before).length() < 0.01);
        // ...and out again, to a limit.
        for (int i = 0; i < 40; ++i) {
            QWheelEvent out(pointer, preview.mapToGlobal(pointer), {}, QPoint(0, -1200), Qt::NoButton, Qt::NoModifier,
                            Qt::NoScrollPhase, false);
            QApplication::sendEvent(&preview, &out);
        }
        QVERIFY(preview.zoom() >= 0.1 && preview.zoom() < 0.11);

        // A drag with the left button moves it (the pen does the same).
        preview.fit();
        QCOMPARE(preview.offset(), QPointF(0, 0));
        QTest::mousePress(&preview, Qt::LeftButton, {}, QPoint(100, 100));
        QTest::mouseMove(&preview, QPoint(130, 90));
        QTest::mouseRelease(&preview, Qt::LeftButton, {}, QPoint(130, 90));
        QCOMPARE(preview.offset(), QPointF(30, -10));
        // Double click fits it again; Ctrl+1 shows it pixel for pixel.
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(200, 150), preview.mapToGlobal(QPointF(200, 150)),
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&preview, &doubleClick);
        QCOMPARE(preview.zoom(), 1.0);
        QCOMPARE(preview.offset(), QPointF(0, 0));
        QTest::keyClick(&preview, Qt::Key_1, Qt::ControlModifier);
        QVERIFY(std::abs(preview.scale() - 1.0) < 1e-9);
        QTest::keyClick(&preview, Qt::Key_F);
        QCOMPARE(preview.zoom(), 1.0);
        // The same size again (the next frame) keeps the view; another size starts over.
        QTest::keyClick(&preview, Qt::Key_1, Qt::ControlModifier);
        preview.setImage(image.copy());
        QVERIFY(preview.zoom() != 1.0);
        preview.setImage(QImage(50, 50, QImage::Format_ARGB32_Premultiplied));
        QCOMPARE(preview.zoom(), 1.0);
    }


    void compositingPreviewDrawsShapes()
    {
        ui::ImagePreview preview;
        preview.resize(400, 300);
        preview.show();
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        preview.setImage(image); // fitted: 2 screen pixels for each of the image's, centered at (200, 150)
        QSignalSpy rectangles(&preview, &ui::ImagePreview::rectangleDrawn);
        QSignalSpy lassos(&preview, &ui::ImagePreview::lassoDrawn);

        // With the Pan tool a drag moves the picture and draws nothing.
        QTest::mousePress(&preview, Qt::LeftButton, {}, QPoint(100, 100));
        QTest::mouseMove(&preview, QPoint(150, 120));
        QTest::mouseRelease(&preview, Qt::LeftButton, {}, QPoint(150, 120));
        QCOMPARE(rectangles.count(), 0);
        preview.fit();

        preview.setTool(ui::ImagePreview::Tool::Rectangle);
        const QPointF a = preview.imagePointAt(QPointF(120, 100)), b = preview.imagePointAt(QPointF(240, 180));
        QTest::mousePress(&preview, Qt::LeftButton, {}, QPoint(120, 100));
        QTest::mouseMove(&preview, QPoint(200, 140));
        QTest::mouseMove(&preview, QPoint(240, 180));
        QTest::mouseRelease(&preview, Qt::LeftButton, {}, QPoint(240, 180));
        QCOMPARE(rectangles.count(), 1);
        const QRectF drawn = rectangles.first().first().toRectF();
        QVERIFY(QLineF(drawn.topLeft(), a).length() < 0.01);
        QVERIFY(QLineF(drawn.bottomRight(), b).length() < 0.01);
        QCOMPARE(preview.offset(), QPointF(0, 0)); // nothing moved
        // Dragged from the other corner, or out of the picture, the rectangle is the same kind and kept inside it.
        QTest::mousePress(&preview, Qt::LeftButton, {}, QPoint(390, 290));
        QTest::mouseMove(&preview, QPoint(300, 200));
        QTest::mouseRelease(&preview, Qt::LeftButton, {}, QPoint(300, 200));
        QCOMPARE(rectangles.count(), 2);
        const QRectF inside = rectangles.last().first().toRectF();
        QVERIFY(QRectF(0, 0, 200, 100).contains(inside));
        // A click, or a rectangle of a pixel, is not a shape.
        QTest::mouseClick(&preview, Qt::LeftButton, {}, QPoint(200, 150));
        QCOMPARE(rectangles.count(), 2);
        // Escape gives up the shape being drawn.
        QTest::mousePress(&preview, Qt::LeftButton, {}, QPoint(100, 100));
        QTest::mouseMove(&preview, QPoint(250, 200));
        QTest::keyClick(&preview, Qt::Key_Escape);
        QTest::mouseRelease(&preview, Qt::LeftButton, {}, QPoint(250, 200));
        QCOMPARE(rectangles.count(), 2);

        // The middle button still moves the picture with a drawing tool.
        QTest::mousePress(&preview, Qt::MiddleButton, {}, QPoint(100, 100));
        QTest::mouseMove(&preview, QPoint(120, 90));
        QTest::mouseRelease(&preview, Qt::MiddleButton, {}, QPoint(120, 90));
        QCOMPARE(preview.offset(), QPointF(20, -10));
        QCOMPARE(rectangles.count(), 2);
        preview.fit();

        // The lasso follows the pen.
        preview.setTool(ui::ImagePreview::Tool::Lasso);
        QTest::mousePress(&preview, Qt::LeftButton, {}, QPoint(150, 100));
        for (const QPoint &p : {QPoint(200, 110), QPoint(250, 150), QPoint(200, 200), QPoint(150, 150), QPoint(148, 102)})
            QTest::mouseMove(&preview, p);
        QTest::mouseRelease(&preview, Qt::LeftButton, {}, QPoint(148, 102));
        QCOMPARE(lassos.count(), 1);
        const QPolygonF shape = lassos.first().first().value<QPolygonF>();
        QVERIFY(shape.size() >= 5);
        QVERIFY(QRectF(0, 0, 200, 100).contains(shape.boundingRect()));
    }


    void maskIsDrawnOnThePreviewWhileItsNodeIsSelected()
    {
        document::DocumentController controller;
        ui::CompositingWindow window(&controller);
        window.show();
        compositing::Graph &network = window.network();
        const compositing::NodeId mask = network.addNode(QStringLiteral("mask"), {0, 200});
        compositing::NodeId other = 0;
        for (const compositing::Node &n : network.nodes())
            if (n.type == QLatin1String("frame"))
                other = n.id;
        QCOMPARE(window.editedMask(), compositing::NodeId(0));
        QCOMPARE(window.preview()->tool(), ui::ImagePreview::Tool::Pan);

        window.graph()->item(mask)->setSelected(true);
        QCOMPARE(window.editedMask(), mask);
        QCOMPARE(window.preview()->tool(), ui::ImagePreview::Tool::Rectangle);

        // A rectangle drawn on the preview becomes an area of the node, in pixels of the page.
        const QSize page(controller.document().width(), controller.document().height());
        const QImage &shown = window.preview()->image();
        QVERIFY(!shown.isNull());
        QTest::mousePress(window.preview(), Qt::LeftButton, {}, QPoint(window.preview()->width() / 3, window.preview()->height() / 3));
        QTest::mouseMove(window.preview(), QPoint(window.preview()->width() / 2, window.preview()->height() / 2));
        QTest::mouseRelease(window.preview(), Qt::LeftButton, {}, QPoint(window.preview()->width() / 2, window.preview()->height() / 2));
        QList<compositing::MaskShape> shapes = compositing::parseMaskShapes(network.node(mask)->data);
        QCOMPARE(shapes.size(), 1);
        QVERIFY(QRectF(QPointF(0, 0), QSizeF(page)).contains(shapes[0].rect));
        // And the preview tints it: the image has an overlay now (the evaluator is asked for the mask).
        QVERIFY(window.preview()->hasOverlay());

        // Another node selected, or none: back to the result.
        window.graph()->item(mask)->setSelected(false);
        QCOMPARE(window.editedMask(), compositing::NodeId(0));
        QVERIFY(!window.preview()->hasOverlay());
        QCOMPARE(window.preview()->tool(), ui::ImagePreview::Tool::Pan);
        window.graph()->item(other)->setSelected(true);
        QCOMPARE(window.editedMask(), compositing::NodeId(0));
        // The mask node goes away while edited: no mask is edited any more.
        window.graph()->item(other)->setSelected(false);
        window.graph()->item(mask)->setSelected(true);
        QCOMPARE(window.editedMask(), mask);
        network.removeNode(mask);
        QCOMPARE(window.editedMask(), compositing::NodeId(0));
    }


    void videoFramesCarryTheCompositing()
    {
        doc::Document d(64, 48);
        compositing::Graph network;
        compositing::NodeId ids[5];
        exampleNetwork(network, ids);
        d.setCompositing(network.toData());
        compositing::Graph loaded;
        loaded.load(d.compositing());
        compositing::Evaluator evaluator;
        const QImage plain = io::renderFinalFrame(d, 0, evaluator, nullptr);
        const QImage composed = io::renderFinalFrame(d, 0, evaluator, &loaded);
        QCOMPARE(plain.size(), QSize(64, 48));
        QCOMPARE(composed.size(), QSize(64, 48));
        QVERIFY(composed != plain); // the example tints the white paper
        QVERIFY(!composed.hasAlphaChannel());
        // A network with nothing wired to Output leaves the frame as it is.
        loaded.disconnect(ids[4], 0);
        QCOMPARE(io::renderFinalFrame(d, 0, evaluator, &loaded), plain);
    }


    void videoIsExportedThroughFfmpeg()
    {
        if (io::findFfmpeg().isEmpty())
            QSKIP("ffmpeg is not installed");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        doc::Document d(63, 47); // odd sizes: the video loses a pixel
        d.setFrameCount(6);
        compositing::Graph network;
        compositing::NodeId ids[5];
        exampleNetwork(network, ids);
        d.setCompositing(network.toData());
        const QString file = dir.filePath(QStringLiteral("out.mp4"));
        QString error;
        int last = 0;
        QVERIFY2(io::exportVideo(d, 1, 4, file, {}, [&last](int done, int total) {
                     last = done;
                     return total == 4;
                 }, &error), qPrintable(error));
        QCOMPARE(last, 4);
        QVERIFY(QFileInfo(file).size() > 200);
        // Stopped part of the way: no file is left.
        const QString stopped = dir.filePath(QStringLiteral("stopped.mp4"));
        QVERIFY(io::exportVideo(d, 0, 5, stopped, {}, [](int done, int) { return done < 2; }, &error));
        QVERIFY(!QFileInfo::exists(stopped));
        // A folder that is not there is an error, with ffmpeg's words.
        QVERIFY(!io::exportVideo(d, 0, 1, dir.filePath(QStringLiteral("no/such/dir/x.mp4")), {}, {}, &error));
        QVERIFY(!error.isEmpty());
    }


    void nodeEditorFollowsTheNetwork()
    {
        compositing::Graph network;
        compositing::NodeId ids[5];
        exampleNetwork(network, ids);
        ui::NodeGraphView view(network);
        QCOMPARE(view.nodeCount(), 5);
        QCOMPARE(view.edgeCount(), 4);
        // Changes made to the network (an undo, later) show in the editor...
        const compositing::NodeId extra = network.addNode(QStringLiteral("blur"));
        QCOMPARE(view.nodeCount(), 6);
        QVERIFY(network.connect(ids[1], 0, extra, 0));
        QCOMPARE(view.edgeCount(), 5);
        // ...and what is done in the editor reaches the network.
        view.item(extra)->setPos(40, 50);
        QCOMPARE(network.node(extra)->pos, QPointF(40, 50));
        view.item(extra)->setSelected(true);
        QTest::keyClick(&view, Qt::Key_Delete);
        QCOMPARE(network.nodes().size(), size_t(5));
        QCOMPARE(view.nodeCount(), 5);
        QCOMPARE(view.edgeCount(), 4);
        // The slider in a node writes its setting to the network.
        network.setParam(ids[1], QStringLiteral("radius"), 20);
        QCOMPARE(network.node(ids[1])->params.value(QStringLiteral("radius")).toInt(), 20);
    }


    static QCheckBox *toggleOf(ui::NodeGraphView &view, compositing::NodeId id)
    {
        QCheckBox *box = nullptr;
        for (QGraphicsItem *child : view.item(id)->childItems())
            if (auto *proxy = dynamic_cast<QGraphicsProxyWidget *>(child))
                if (auto *b = proxy->widget()->findChild<QCheckBox *>())
                    box = b;
        return box;
    }


    void nodeToggleIsKeptAndShownAgain()
    {
        compositing::Graph network;
        const compositing::NodeId noise = network.addNode(QStringLiteral("sensornoise"));
        ui::NodeGraphView view(network);
        view.show();
        QCheckBox *box = toggleOf(view, noise);
        QVERIFY(box && !box->isChecked());
        box->click();
        QCOMPARE(network.node(noise)->params.value(QStringLiteral("animate")).toInt(), 1);
        compositing::Graph reloaded; // stored and loaded again
        reloaded.load(network.toData());
        ui::NodeGraphView other(reloaded);
        QCheckBox *again = toggleOf(other, noise);
        QVERIFY(again && again->isChecked());
    }


    void toggleSurvivesTheWindowAndTheDocument()
    {
        document::DocumentController controller;
        compositing::NodeId noise = 0;
        {
            ui::CompositingWindow window(&controller);
            window.show();
            noise = window.network().addNode(QStringLiteral("sensornoise"), {0, 300});
            QCheckBox *box = toggleOf(*window.graph(), noise);
            QVERIFY(box);
            box->click();
        } // closing keeps the edit
        ui::CompositingWindow again(&controller);
        QCOMPARE(again.network().node(noise)->params.value(QStringLiteral("animate")).toInt(), 1);
    }


    void resetLeavesNoOldWires()
    {
        compositing::Graph network;
        compositing::NodeId ids[5];
        exampleNetwork(network, ids);
        ui::NodeGraphView view(network);
        QCOMPARE(view.edgeCount(), 4);
        network.load({}); // another project: nothing is left of the old network
        QCOMPARE(view.nodeCount(), 0);
        int wires = 0;
        for (QGraphicsItem *item : view.scene()->items())
            wires += dynamic_cast<ui::EdgeItem *>(item) ? 1 : 0;
        QCOMPARE(wires, 0);
    }


    void compositingViewsHaveButtonsForZoomPanAndCut()
    {
        compositing::Graph network;
        compositing::NodeId ids[5];
        exampleNetwork(network, ids);
        ui::NodeGraphView view(network);
        view.resize(600, 400);
        view.show();
        view.fitAll();
        const qreal before = view.transform().m11();
        view.zoomBy(1.25);
        QVERIFY(view.transform().m11() > before);
        view.zoomBy(0.8);
        QVERIFY(qAbs(view.transform().m11() - before) < 1e-6);
        // Pan mode: the left button moves the view instead of selecting.
        view.setMode(ui::NodeGraphView::Mode::Pan);
        const QPointF centerBefore = view.mapToScene(view.viewport()->rect().center());
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseMove(view.viewport(), QPoint(60, 50));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, QPoint(60, 50));
        QVERIFY(view.mapToScene(view.viewport()->rect().center()) != centerBefore);
        // Cut mode: a line across a wire cuts it, without Shift.
        view.setMode(ui::NodeGraphView::Mode::Cut);
        QCOMPARE(view.edgeCount(), 4);
        ui::EdgeItem *wire = nullptr;
        for (QGraphicsItem *item : view.scene()->items())
            if (auto *e = dynamic_cast<ui::EdgeItem *>(item))
                wire = e;
        QVERIFY(wire);
        const QPointF mid = wire->path().pointAtPercent(0.5);
        const QPoint a = view.mapFromScene(mid + QPointF(0, -30)), b = view.mapFromScene(mid + QPointF(0, 30));
        QTest::mousePress(view.viewport(), Qt::LeftButton, {}, a);
        QTest::mouseMove(view.viewport(), (a + b) / 2);
        QTest::mouseMove(view.viewport(), b);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, {}, b);
        QVERIFY(view.edgeCount() < 4);

        ui::ImagePreview preview;
        preview.resize(400, 300);
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        preview.setImage(image);
        preview.zoomBy(2);
        QCOMPARE(preview.zoom(), 2.0);
    }


    void nodeColorIsPickedWithoutBlocking()
    {
        compositing::Graph network;
        compositing::NodeId ids[5];
        exampleNetwork(network, ids);
        ui::NodeGraphView graph(network);
        graph.show();
        ui::NodeItem *color = graph.item(ids[3]);
        QPushButton *swatch = nullptr;
        for (QGraphicsItem *child : color->childItems())
            if (auto *proxy = dynamic_cast<QGraphicsProxyWidget *>(child))
                swatch = proxy->widget()->findChild<QPushButton *>();
        QVERIFY(swatch);
        swatch->click(); // must return: the dialog does not block
        QColorDialog *dialog = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<QColorDialog *>(top))
                dialog = d;
        QVERIFY(dialog);
        const QString key = QStringLiteral("color");
        dialog->setCurrentColor(QColor(10, 200, 30, 255));
        QCOMPARE(network.node(ids[3])->params.value(key).value<QColor>(), QColor(10, 200, 30, 255));
        dialog->reject(); // cancelled: the old color is back
        QCOMPARE(network.node(ids[3])->params.value(key).value<QColor>(), QColor(150, 60, 190, 110));
    }


    void nodeGraphWiresAreMovedAndCut()
    {
        compositing::Graph network;
        compositing::NodeId ids[5];
        exampleNetwork(network, ids);
        ui::NodeGraphView graph(network);
        graph.resize(1000, 600);
        graph.show();
        graph.fitWidth(0.8);
        ui::NodeItem *merge = graph.item(ids[2]);
        QCOMPARE(graph.edgeCount(), 4);
        auto viewPos = [&](const QPointF &scenePos) { return graph.mapFromScene(scenePos); };

        // Near a socket (not on it) it lights up.
        const QPointF back = merge->socketPos(false, 0);
        QTest::mouseMove(graph.viewport(), viewPos(back + QPointF(-10, 6)));
        QVERIFY(merge->hotSocket() == 0);

        // Pick the wire up by its end and let go over nothing: it is gone.
        QTest::mousePress(graph.viewport(), Qt::LeftButton, {}, viewPos(back));
        QTest::mouseMove(graph.viewport(), viewPos(back + QPointF(-60, 200)));
        QTest::mouseRelease(graph.viewport(), Qt::LeftButton, {}, viewPos(back + QPointF(-60, 200)));
        QCOMPARE(graph.edgeCount(), 3);
        QVERIFY(!network.linkInto(ids[2], 0));

        // Shift and drag across a wire cuts it.
        const QPointF front = merge->socketPos(false, 1);
        const QPointF a = front + QPointF(-60, -30), b = front + QPointF(-60, 30);
        QTest::mousePress(graph.viewport(), Qt::LeftButton, Qt::ShiftModifier, viewPos(a));
        QTest::mouseMove(graph.viewport(), viewPos((a + b) / 2));
        QTest::mouseMove(graph.viewport(), viewPos(b));
        QTest::mouseRelease(graph.viewport(), Qt::LeftButton, Qt::ShiftModifier, viewPos(b));
        QCOMPARE(graph.edgeCount(), 2);
        QVERIFY(!network.linkInto(ids[2], 1));
    }

};

QTEST_MAIN(TestUiCompositing)
#include "tst_ui_compositing.moc"
