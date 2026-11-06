// Tests for the UI layer: the canvas tools: move, selection, transform, fill, paste.

#include "test_common.h"

class TestUiTools : public QObject
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


    // The wrench sits under the panel, outside the scrolling: visible however
    // short the dock is.
    void wrenchStaysUnderTheScrollingPanel()
    {
        tools::ToolState state;
        auto *panel = new ui::ToolPropertiesPanel(&state);
        ui::PanelDock dock(QStringLiteral("Tool properties"), QStringLiteral("dock.test"), panel, nullptr,
                           panel->footer());
        auto *wrench = dock.findChild<QToolButton *>(QStringLiteral("advancedToolSettings"));
        auto *scroll = dock.findChild<QScrollArea *>();
        QVERIFY(wrench && scroll);
        QVERIFY(!scroll->isAncestorOf(wrench));

        dock.resize(260, 160);
        dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dock));
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(wrench->isVisible());
        QVERIFY(dock.rect().contains(QRect(wrench->mapTo(&dock, QPoint()), wrench->size())));

        state.setActiveTool(tools::ToolId::Puppet); // no brush options, no wrench
        QVERIFY(!wrench->isVisible());
    }


    // Clear empties the drawing at the current layer and frame in one undo
    // step; the drawing and its exposures stay.
    void clearEmptiesTheCurrentDrawing()
    {
        document::DocumentController controller;
        QImage image(40, 30, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        controller.importImage(image, QStringLiteral("pic"));
        const doc::Document &d = controller.document();
        const doc::Cel &cel = *d.cel(controller.currentCel());
        const doc::DrawingId drawing = cel.timing().drawingAt(0);
        QVERIFY(controller.canClearCurrentLayer());

        QVERIFY(controller.clearCurrentLayer());
        QVERIFY(cel.drawing(drawing)->layers.front().image.isEmpty());
        QCOMPARE(cel.timing().drawingAt(0), drawing); // still exposed, just empty
        QVERIFY(!controller.canClearCurrentLayer());
        QCOMPARE(controller.undoText(), QStringLiteral("Undo clear"));

        controller.undo();
        QVERIFY(!cel.drawing(drawing)->layers.front().image.isEmpty());
        controller.redo();
        QVERIFY(cel.drawing(drawing)->layers.front().image.isEmpty());

        QString reason;
        QVERIFY(!controller.clearCurrentLayer(&reason)); // nothing left
        QVERIFY(!reason.isEmpty());
    }


    void clearRespectsLockedAndHiddenLayers()
    {
        document::DocumentController controller;
        QImage red(10, 10, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("red"));
        const doc::CelId id = controller.currentCel();
        doc::CelProperties props = controller.document().cel(id)->properties();

        for (bool locked : {true, false}) {
            doc::CelProperties p = props;
            p.locked = locked;
            p.visible = locked; // first locked, then hidden
            controller.execute(doc::makeSetCelProperties(controller.document(), id, p));
            QString reason;
            QVERIFY(!controller.canClearCurrentLayer());
            QVERIFY(!controller.clearCurrentLayer(&reason));
            QVERIFY(reason.contains(locked ? QStringLiteral("locked") : QStringLiteral("hidden")));
        }
    }


    void clearOnAnEmptyFrameDoesNothing()
    {
        document::DocumentController controller; // one layer, no drawings
        QVERIFY(!controller.canClearCurrentLayer());
        QVERIFY(!controller.clearCurrentLayer());
        QVERIFY(!controller.canUndo());
    }


    // The layer tools work on is one of the drawing shown: going to another
    // drawing keeps the layer of the same name, else the top one; a drawing
    // made by painting on an empty frame names its layer after it.
    void currentLayerFollowsTheDrawings()
    {
        document::DocumentController controller;
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        auto drawingWith = [&](int frame, std::vector<std::string> names) {
            doc::NewDrawing nd = doc::makeNewDrawing(d, cel, frame, 1, names.front());
            const doc::DrawingId id = nd.id;
            controller.execute(std::move(nd.command));
            for (std::size_t i = 1; i < names.size(); ++i)
                controller.execute(std::make_unique<doc::AddLayerCommand>(d, cel, id, int(i),
                                                                          doc::LayerProperties{names[i]}));
            return d.cel(cel)->drawing(id);
        };
        const doc::Drawing *first = drawingWith(0, {"Line", "Color"});
        const doc::Drawing *second = drawingWith(1, {"Color", "Line", "Shadow"});
        QCOMPARE(controller.currentLayer(), first->layers[0].id); // the one it was made with, still there

        QSignalSpy changed(&controller, &document::DocumentController::currentLayerChanged);
        controller.setCurrentLayer(first->layers[1].id); // Color
        controller.setCurrentLayer(first->layers[0].id); // Line
        QCOMPARE(changed.count(), 2);
        controller.setCurrentFrame(1);
        QCOMPARE(controller.currentLayer(), second->layers[1].id); // Line again
        controller.setCurrentLayer(second->layers[2].id); // Shadow: not in the first drawing
        controller.setCurrentFrame(0);
        QCOMPARE(controller.currentLayer(), first->layers.back().id);
        controller.setCurrentFrame(5);
        QCOMPARE(controller.currentLayer(), doc::kNoId); // an empty frame
        QCOMPARE(controller.currentDrawing(), nullptr);

        const auto target = controller.beginPaint(true); // the last one worked on was Color
        QVERIFY(target);
        const doc::Drawing *made = controller.currentDrawing();
        QVERIFY(made);
        QCOMPARE(made->layers.size(), std::size_t(1));
        QCOMPARE(made->layers[0].properties.name, std::string("Color"));
        QCOMPARE(target->layer, made->layers[0].id);
        QCOMPARE(controller.currentLayer(), target->layer);
        controller.cancelPaint();
        QCOMPARE(controller.currentDrawing(), nullptr);

        // A locked layer is not painted.
        controller.setCurrentFrame(0);
        doc::LayerProperties p = first->layers.back().properties;
        p.locked = true;
        controller.execute(doc::makeSetLayerProperties(d, cel, first->id, first->layers.back().id, p));
        QString reason;
        QVERIFY(!controller.beginPaint(true, &reason));
        QVERIFY(reason.contains(QStringLiteral("locked")));
        QVERIFY(!controller.canClearCurrentLayer());
    }


    void importedImageGetsItsOwnLayer()
    {
        document::DocumentController controller; // 1920×1080, one layer
        const doc::Document &d = controller.document();
        QImage image(100, 50, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("photo"));

        QCOMPARE(d.celCount(), 2);
        const doc::Cel &cel = d.celAt(1); // above the first one
        QCOMPARE(cel.properties().name, std::string("photo"));
        QCOMPARE(controller.currentCel(), cel.id());
        const doc::DrawingId drawing = cel.timing().drawingAt(0);
        QVERIFY(drawing != doc::kNoId);
        QCOMPARE(cel.timing().drawingAt(d.frameCount() - 1), drawing); // held for the whole scene
        const doc::TiledImage &pixels = cel.drawing(drawing)->layers.front().image;
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xffff0000)); // centered
        QCOMPARE(pixels.pixel(909, 540), doc::Pixel(0));
        QCOMPARE(pixels.pixel(910, 515), doc::Pixel(0xffff0000));

        controller.undo(); // one step takes it all back
        QCOMPARE(d.celCount(), 1);
    }


    void moveToolMovesTheDrawingInOneStep()
    {
        document::DocumentController controller;
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("dot")); // 955..964, 535..544
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;

        QStringList messages;
        canvas::MoveTool tool(&controller, [&](const QString &m) { messages << m; });
        tool.press({100, 100}, Qt::NoModifier);
        QVERIFY(tool.isActive());
        tool.move({130.4, 110}, Qt::NoModifier);
        tool.move({150, 120.2}, Qt::NoModifier);
        tool.release({150, 120.2}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QCOMPARE(qAlpha(pixels.pixel(960, 540)), 0);
        QCOMPARE(pixels.pixel(1010, 560), doc::Pixel(0xff00ff00)); // moved by (50, 20)

        controller.undo(); // the whole drag at once
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xff00ff00));
        controller.undo(); // then the import
        QCOMPARE(controller.document().celCount(), 1);
        QVERIFY(messages.isEmpty());
    }


    // The move takes what the Layers panel has chosen: several layers, a
    // frame (all its layers), a cel (all the layers of all its frames); one
    // undo step, locked layers left as they are.
    void moveToolMovesTheChosenLayers()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 700);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        const doc::CelId cel = controller.currentCel();
        struct Made { doc::DrawingId drawing; doc::LayerId line, color; };
        std::vector<Made> frames;
        for (int f : {0, 1}) {
            doc::NewDrawing nd = doc::makeNewDrawing(d, cel, f, 1, "Line");
            controller.execute(std::move(nd.command));
            auto color = std::make_unique<doc::AddLayerCommand>(d, cel, nd.id, 1, doc::LayerProperties{"Color"});
            const doc::LayerId colorId = color->layerId();
            controller.execute(std::move(color));
            frames.push_back({nd.id, nd.layer, colorId});
            d.layer(cel, nd.id, nd.layer)->image.setPixel(100, 100, 0xff000000);
            d.layer(cel, nd.id, colorId)->image.setPixel(100, 100, 0xff000000);
        }
        QCoreApplication::processEvents();
        auto *tree = panel.findChild<QTreeWidget *>();
        auto moved = [&](const Made &m, doc::LayerId layer) {
            return d.layer(cel, m.drawing, layer)->image.pixel(110, 100) == doc::Pixel(0xff000000);
        };
        canvas::MoveTool tool(&controller, [](const QString &) {});
        auto drag = [&] {
            tool.press({0, 0}, Qt::NoModifier);
            tool.move({10, 0}, Qt::NoModifier);
            tool.release({10, 0}, Qt::NoModifier);
        };

        tree->setCurrentItem(panel.celItem(cel)); // the whole cel
        drag();
        for (const Made &m : frames)
            QVERIFY(moved(m, m.line) && moved(m, m.color));
        controller.undo(); // all at once
        QCoreApplication::processEvents();
        for (const Made &m : frames)
            QVERIFY(!moved(m, m.line) && !moved(m, m.color));

        tree->setCurrentItem(panel.frameItem(frames[1].drawing)); // one frame: both its layers
        drag();
        QVERIFY(moved(frames[1], frames[1].line) && moved(frames[1], frames[1].color));
        QVERIFY(!moved(frames[0], frames[0].line) && !moved(frames[0], frames[0].color));
        controller.undo();
        QCoreApplication::processEvents();

        // Two layers of a frame; one of them locked stays.
        tree->setCurrentItem(panel.layerItem(frames[1].line));
        panel.layerItem(frames[1].color)->setSelected(true);
        doc::LayerProperties locked = d.layer(cel, frames[1].drawing, frames[1].color)->properties;
        locked.locked = true;
        controller.execute(doc::makeSetLayerProperties(d, cel, frames[1].drawing, frames[1].color, locked));
        QCoreApplication::processEvents();
        QCOMPARE(controller.layerSelection().size(), std::size_t(2));
        drag();
        QVERIFY(moved(frames[1], frames[1].line));
        QVERIFY(!moved(frames[1], frames[1].color));
    }


    void moveToolShiftKeepsOneAxisAndCancelRestores()
    {
        document::DocumentController controller;
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("dot"));
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;

        canvas::MoveTool tool(&controller, [](const QString &) {});
        tool.press({0, 0}, Qt::NoModifier);
        tool.move({40, 7}, Qt::ShiftModifier); // horizontal only
        QCoreApplication::processEvents(); // the picture follows on the next turn of the event loop
        QCOMPARE(pixels.pixel(1000, 540), doc::Pixel(0xff00ff00));
        tool.cancel();
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xff00ff00));
        QCOMPARE(qAlpha(pixels.pixel(1000, 540)), 0);
        QVERIFY(!controller.isPainting());
    }


    void moveToolRefusesEmptyFrames()
    {
        document::DocumentController controller; // Layer 1 has no drawing yet
        QStringList messages;
        canvas::MoveTool tool(&controller, [&](const QString &m) { messages << m; });
        tool.press({10, 10}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QCOMPARE(messages.size(), 1);
    }


    void selectionToolsSetAndClearTheSelection()
    {
        document::DocumentController controller;
        canvas::SelectionTool rect(&controller, canvas::SelectionTool::Shape::Rectangle);
        rect.press({10.2, 20.4}, Qt::NoModifier);
        rect.move({50, 40}, Qt::NoModifier);
        rect.release({60.4, 70.6}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(10, 20, 50, 51)); // whole pixels

        rect.press({300, 300}, Qt::NoModifier); // a click deselects
        rect.release({300.5, 300}, Qt::NoModifier);
        QVERIFY(!controller.hasSelection());

        rect.press({-100, -100}, Qt::NoModifier); // clipped to the page
        rect.release({10, 10}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(0, 0, 10, 10));

        canvas::SelectionTool lasso(&controller, canvas::SelectionTool::Shape::Lasso);
        lasso.press({100, 100}, Qt::NoModifier);
        lasso.move({200, 100}, Qt::NoModifier);
        lasso.release({200, 200}, Qt::NoModifier); // triangle (100,100) (200,100) (200,200)
        QVERIFY(controller.selection().contains(QPointF(190, 110)));
        QVERIFY(!controller.selection().contains(QPointF(110, 190))); // the triangle's other half
    }


    void moveToolMovesOnlyTheSelection()
    {
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;

        QPainterPath left;
        left.addRect(900, 500, 60, 100); // x 900..959: the bar's left half
        controller.setSelection(left);
        canvas::MoveTool tool(&controller, [](const QString &) {});
        tool.press({0, 0}, Qt::NoModifier);
        tool.release({0, 100}, Qt::NoModifier);

        QCOMPARE(pixels.pixel(930, 640), doc::Pixel(0xff00ff00)); // left half moved down
        QCOMPARE(qAlpha(pixels.pixel(930, 540)), 0);
        QCOMPARE(pixels.pixel(990, 540), doc::Pixel(0xff00ff00)); // right half stayed
        QCOMPARE(qAlpha(pixels.pixel(990, 640)), 0);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 600, 60, 100)); // followed
    }


    // The selection follows the move while dragging, and the move is undone
    // with it: pixels and selection in one step.
    void moveCarriesTheSelectionLiveAndUndoesIt()
    {
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        const doc::Cel &cel = controller.document().celAt(1);
        const doc::TiledImage &pixels = cel.drawing(cel.timing().drawingAt(0))->layers.front().image;
        QPainterPath left;
        left.addRect(900, 500, 60, 100);
        controller.setSelection(left);

        canvas::MoveTool tool(&controller, [](const QString &) {});
        tool.press({0, 0}, Qt::NoModifier);
        tool.move({0, 50}, Qt::NoModifier);
        QCoreApplication::processEvents(); // the picture follows on the next turn of the event loop
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 550, 60, 100)); // live
        tool.release({0, 100}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 600, 60, 100));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo move"));

        controller.undo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));
        QCOMPARE(pixels.pixel(930, 540), doc::Pixel(0xff00ff00));
        controller.redo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 600, 60, 100));
        QCOMPARE(pixels.pixel(930, 640), doc::Pixel(0xff00ff00));

        tool.press({0, 0}, Qt::NoModifier); // a cancelled move puts the selection back
        tool.move({30, 0}, Qt::NoModifier);
        QCoreApplication::processEvents(); // the picture follows on the next turn of the event loop
        QCOMPARE(controller.selection().boundingRect(), QRectF(930, 600, 60, 100));
        tool.cancel();
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 600, 60, 100));

        QPainterPath empty; // over transparent pixels only the selection moves: still undoable
        empty.addRect(100, 100, 50, 50);
        controller.setSelection(empty);
        tool.press({0, 0}, Qt::NoModifier);
        tool.release({20, 0}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(120, 100, 50, 50));
        controller.undo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(100, 100, 50, 50));
    }


    // While drawn the lasso is an open line; the release closes it.
    void lassoShowsNoClosingLineWhileDrawn()
    {
        document::DocumentController controller;
        canvas::SelectionTool lasso(&controller, canvas::SelectionTool::Shape::Lasso);
        lasso.press({10, 10}, Qt::NoModifier);
        lasso.move({90, 10}, Qt::NoModifier);
        lasso.move({90, 90}, Qt::NoModifier);

        const canvas::ViewTransform view;
        QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter p(&image);
            lasso.paintOverlay(p, view);
        }
        const QPoint onEdge = view.documentToView().map(QPointF(50, 10)).toPoint();
        const QPoint onClosingLine = view.documentToView().map(QPointF(50, 50)).toPoint();
        QVERIFY(qAlpha(image.pixel(onEdge)) > 0);
        QCOMPARE(qAlpha(image.pixel(onClosingLine)), 0);

        lasso.release({90, 90}, Qt::NoModifier);
        QVERIFY(controller.selection().contains(QPointF(70, 30))); // closed: a triangle
    }


    // The bucket fills inside the lines, under their edge, within the
    // selection, as one undo step.
    void fillToolFillsInsideTheLines()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage ring(100, 100, QImage::Format_ARGB32);
        ring.fill(Qt::transparent);
        {
            QPainter p(&ring);
            p.setPen(QPen(Qt::black, 4));
            p.drawRect(10, 10, 80, 80);
        }
        controller.importImage(ring, QStringLiteral("lines")); // at 910..1009, 490..589
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
        canvas::FrameRenderer frame;
        QStringList messages;
        canvas::FillTool tool(&controller, &state, [&]() -> const QImage & {
            frame.renderAll(controller.document(), controller.currentFrame());
            return frame.image();
        }, [&](const QString &text) { messages << text; });

        state.setColor(QColor(255, 0, 0));
        tool.press({960, 540}, Qt::NoModifier); // inside
        QCOMPARE(pixel(960, 540), doc::Pixel(0xffff0000));
        QCOMPARE(qAlpha(pixel(900, 540)), 0);       // outside stays empty
        QCOMPARE(pixel(920, 540), doc::Pixel(0xff000000)); // the line stays on top
        QCOMPARE(pixel(923, 540), doc::Pixel(0xffff0000)); // up to it
        QCOMPARE(controller.undoText(), QStringLiteral("Undo fill"));
        controller.undo();
        QCOMPARE(qAlpha(pixel(960, 540)), 0);

        QPainterPath left; // kept inside the selection
        left.addRect(900, 480, 60, 120);
        controller.setSelection(left);
        tool.press({950, 540}, Qt::NoModifier);
        QCOMPARE(pixel(950, 540), doc::Pixel(0xffff0000));
        QCOMPARE(qAlpha(pixel(980, 540)), 0);
        tool.press({980, 540}, Qt::NoModifier); // outside it: refused
        QCOMPARE(qAlpha(pixel(980, 540)), 0);
        QVERIFY(!messages.isEmpty());
    }


    // The transform moves, scales and rotates the selected pixels with their
    // selection; Enter applies (one undo step), Esc drops it, either goes back.
    void transformMovesScalesAndRotatesTheSelection()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        const doc::Cel &cel = controller.document().celAt(1);
        auto pixel = [&](int x, int y) { return cel.drawing(cel.timing().drawingAt(0))->layers.front().image.pixel(x, y); };
        const doc::Pixel green = pixel(930, 540);
        QPainterPath left;
        left.addRect(900, 500, 60, 100); // the bar's left half
        controller.setSelection(left);
        state.setActiveTool(tools::ToolId::LassoSelect);
        state.setActiveTool(tools::ToolId::Transform);

        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.activated();
        QVERIFY(tool.hasPendingEdit());
        QCOMPARE(tool.box(), QRect(900, 500, 60, 100)); // the selection's own box, not the pixels'

        tool.press({930, 540}, Qt::NoModifier); // inside: moves
        tool.move({930, 590}, Qt::NoModifier);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 550, 60, 100)); // live
        tool.release({930, 640}, Qt::NoModifier);
        QCOMPARE(pixel(930, 640), green);
        QCOMPARE(qAlpha(pixel(930, 540)), 0);
        QCOMPARE(pixel(990, 540), green); // outside the selection: stays
        QVERIFY(tool.keyPress(Qt::Key_Return));
        QVERIFY(!tool.hasPendingEdit());
        QCOMPARE(state.activeTool(), tools::ToolId::LassoSelect);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo transform"));
        controller.undo();
        QCOMPARE(pixel(930, 540), green);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));

        tool.activated(); // the bottom-right corner, twice as far from the top-left one
        tool.press({960, 600}, Qt::NoModifier);
        tool.release({1020, 700}, Qt::NoModifier);
        QCOMPARE(tool.transform().mapRect(QRectF(tool.box())), QRectF(900, 500, 120, 200));
        QCOMPARE(pixel(1000, 580), green);
        QVERIFY(tool.keyPress(Qt::Key_Escape)); // dropped
        QCOMPARE(qAlpha(pixel(1000, 580)), 0);
        QCOMPARE(pixel(930, 540), green);
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));

        tool.activated(); // outside the box: a quarter turn around its center (930, 550)
        tool.press({1000, 550}, Qt::NoModifier);
        tool.release({930, 620}, Qt::NoModifier);
        const QPointF axis = tool.transform().map(QPointF(1, 0)) - tool.transform().map(QPointF(0, 0));
        QVERIFY(std::abs(axis.x()) < 1e-6 && std::abs(axis.y() - 1) < 1e-6);
        QCOMPARE(pixel(940, 555), green); // the bar now stands upright
        QCOMPARE(qAlpha(pixel(915, 540)), 0);
        tool.commit();
        QCOMPARE(controller.undoText(), QStringLiteral("Undo transform"));
    }


    // A burst of pen samples is drawn once, at the last position, and the end of the drag shows it at once.
    void moveToolDrawsOnceForABurstOfSamples()
    {
        document::DocumentController controller;
        QImage image(60, 60, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        QVERIFY(controller.pasteImage(image, QPoint(100, 100)));
        controller.setSelection({});
        int painted = 0;
        connect(&controller, &document::DocumentController::pixelsChanged, this, [&painted] { ++painted; });
        canvas::MoveTool tool(&controller, [](const QString &) {});
        tool.press({110, 110}, Qt::NoModifier);
        painted = 0;
        for (int i = 1; i <= 50; ++i)
            tool.move({110.0 + i, 110.0}, Qt::NoModifier);
        QCOMPARE(painted, 0); // nothing drawn until the event loop turns
        QCoreApplication::processEvents();
        QCOMPARE(painted, 1);
        tool.move({200, 110}, Qt::NoModifier);
        tool.release({200, 110}, Qt::NoModifier); // no wait at the end
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        QCOMPARE(cel.drawingAt(controller.currentFrame())->layers.front().image.pixel(240, 130), doc::Pixel(0xffff0000));
    }


    // An image bigger than the page is pasted whole: the part off the page is kept and selected, so moving it
    // brings that part onto the page.
    void pastedImageBiggerThanThePageIsKeptWhole()
    {
        tools::ToolState state;
        document::DocumentController controller;
        const int w = controller.document().width(), h = controller.document().height();
        QImage image(w + 480, h + 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        QPainter p(&image);
        p.fillRect(0, 0, 240, image.height(), Qt::green); // the strip that falls off the left edge
        p.end();
        QVERIFY(controller.pasteImage(image, std::nullopt));
        const QRectF whole(-240, -60, image.width(), image.height());
        QCOMPARE(controller.selection().boundingRect(), whole);
        auto pixel = [&controller](int x, int y) {
            const doc::Cel &cel = *controller.document().cel(controller.currentCel());
            return cel.drawingAt(controller.currentFrame())->layers.front().image.pixel(x, y);
        };
        QCOMPARE(pixel(-100, 10), doc::Pixel(0xff00ff00)); // off the page, and still there
        QCOMPARE(pixel(100, 10), doc::Pixel(0xffff0000));

        state.setActiveTool(tools::ToolId::Transform);
        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.activated();
        QCOMPARE(tool.box(), whole.toRect()); // the box is the whole image, not the page's part
        tool.press({500, 500}, Qt::NoModifier);
        tool.move({800, 500}, Qt::NoModifier);
        tool.release({800, 500}, Qt::NoModifier); // 300 to the right
        QCOMPARE(pixel(100, 10), doc::Pixel(0xff00ff00)); // what was off the page is on it now
        QVERIFY(tool.keyPress(Qt::Key_Return));
        QCOMPARE(pixel(100, 10), doc::Pixel(0xff00ff00));
    }


    // Just outside a corner of the box (not on its handle) a drag turns it, with a turning
    // arrow for the cursor; the handle itself scales, with arrows along its diagonal.
    void transformTurnsFromJustOutsideACorner()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("square")); // 910..1009, 490..589
        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.setViewZoom(1.0);
        tool.activated();
        QCOMPARE(tool.box(), QRect(910, 490, 100, 100));

        tool.hover({1010, 590}); // on the bottom-right handle
        QCOMPARE(tool.cursor().shape(), Qt::SizeFDiagCursor);
        tool.hover({960, 540}); // inside
        QCOMPARE(tool.cursor().shape(), Qt::SizeAllCursor);
        tool.hover({1020, 600}); // a little out of the corner
        QCOMPARE(tool.cursor().shape(), Qt::BitmapCursor); // the turning arrow
        tool.hover({1010, 490}); // the top-right handle: the other diagonal
        QCOMPARE(tool.cursor().shape(), Qt::SizeBDiagCursor);
        tool.hover({960, 590}); // the middle of the bottom side
        QCOMPARE(tool.cursor().shape(), Qt::SizeVerCursor);

        tool.press({1020, 600}, Qt::NoModifier); // 14 px out of the corner: turns
        tool.move({960, 650}, Qt::NoModifier);
        tool.release({960, 650}, Qt::NoModifier);
        const QTransform t = tool.transform();
        QCOMPARE(t.m11(), t.m22()); // a rotation: no scaling, no shear
        QVERIFY(std::abs(t.m12()) > 0.1);
        QVERIFY(std::abs(t.m11() * t.m11() + t.m12() * t.m12() - 1.0) < 1e-9);
        tool.cancel();
    }


    // "Fill refer to other layers" finds the area in the union of the layers shown (the lines on another
    // layer stop it), "Fill" in the layer it fills; both fill in the current layer.
    void fillCanReferToOtherLayers()
    {
        tools::ToolState state;
        document::DocumentController controller;
        const auto presets = tools::builtInPresets(tools::ToolId::Fill);
        QCOMPARE(presets[1].name, QStringLiteral("Fill refer to other layers"));
        state.setColor(Qt::blue);
        state.setOpacity(tools::ToolId::Fill, 1.0);

        // A square of lines on one layer; then another layer, empty, is the one to fill.
        QString reason;
        const auto target = controller.beginPaint(true, &reason);
        QVERIFY(target);
        const doc::TiledImage before = *target->image;
        for (int i = 100; i <= 300; ++i)
            for (int t = 0; t < 3; ++t) {
                target->image->setPixel(i, 100 + t, 0xff000000);
                target->image->setPixel(i, 300 + t, 0xff000000);
                target->image->setPixel(100 + t, i, 0xff000000);
                target->image->setPixel(300 + t, i, 0xff000000);
            }
        controller.endPaint(before);
        const doc::LayerId lines = controller.currentLayer();
        QVERIFY(controller.addLayer(true));
        const doc::LayerId color = controller.currentLayer();
        QVERIFY(color != lines);

        canvas::FrameRenderer frame;
        canvas::FillTool tool(&controller, &state, [&]() -> const QImage & {
            frame.renderAll(controller.document(), controller.currentFrame());
            return frame.image();
        }, [](const QString &) {});
        auto pixel = [&](int x, int y) {
            for (const doc::Layer &l : controller.currentDrawing()->layers)
                if (l.id == color)
                    return l.image.pixel(x, y);
            return doc::Pixel(0);
        };

        tools::applyPreset(state, presets[1]);
        QVERIFY(state.fillSettings().allLayers);
        tool.press({200, 200}, Qt::NoModifier);
        QCOMPARE(pixel(200, 200), doc::Pixel(0xff0000ff)); // inside the lines of the other layer
        QCOMPARE(qAlpha(pixel(50, 50)), 0);                 // and not outside them
        controller.undo();

        tools::applyPreset(state, presets[0]);
        QVERIFY(!state.fillSettings().allLayers);
        tool.press({200, 200}, Qt::NoModifier);
        QCOMPARE(pixel(200, 200), doc::Pixel(0xff0000ff)); // this layer is empty: the area is all of it
        QCOMPARE(pixel(50, 50), doc::Pixel(0xff0000ff));
    }


    // The lasso fill sub tool: an outline drawn freehand, its inside filled
    // over what is there, within the selection, as one undo step.
    void lassoFillFillsTheOutline()
    {
        tools::ToolState state;
        document::DocumentController controller;
        const auto presets = tools::builtInPresets(tools::ToolId::Fill);
        QCOMPARE(presets.size(), 3);
        state.setOpacity(tools::ToolId::Fill, 0.5);
        tools::applyPreset(state, presets[2]);
        QCOMPARE(state.fillSettings().mode, tools::FillSettings::Mode::Lasso);
        QCOMPARE(state.settings(tools::ToolId::Fill).opacity, 0.5); // a fill sub tool keeps the rest
        state.setOpacity(tools::ToolId::Fill, 1.0);
        state.setColor(Qt::blue);

        canvas::FrameRenderer frame;
        canvas::FillTool tool(&controller, &state, [&]() -> const QImage & { return frame.image(); },
                              [](const QString &) {});
        tool.press({100, 100}, Qt::NoModifier);
        QVERIFY(tool.isActive());
        tool.move({300, 100}, Qt::NoModifier);
        tool.move({300, 300}, Qt::NoModifier);
        tool.release({100, 300}, Qt::NoModifier); // a square, closed by the release
        QVERIFY(!tool.isActive());
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
        QCOMPARE(pixel(200, 200), doc::Pixel(0xff0000ff));
        QCOMPARE(qAlpha(pixel(350, 200)), 0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo fill"));

        QPainterPath left; // kept inside the selection
        left.addRect(400, 400, 50, 200);
        controller.setSelection(left);
        state.setColor(Qt::red);
        tool.press({420, 450}, Qt::NoModifier);
        tool.move({500, 450}, Qt::NoModifier);
        tool.release({500, 550}, Qt::NoModifier);
        QCOMPARE(pixel(440, 460), doc::Pixel(0xffff0000)); // in the triangle and the selection
        QCOMPARE(qAlpha(pixel(480, 460)), 0);               // in the triangle, out of the selection

        tool.press({600, 600}, Qt::NoModifier); // cancelled: nothing
        tool.move({700, 600}, Qt::NoModifier);
        tool.cancel();
        QVERIFY(!tool.isActive());
    }


    // Copy takes the selected pixels and where they were; paste puts them on
    // the current layer, in place, on this frame, selected: one undo step.
    void copyAndPasteSelectedPixels()
    {
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        QPainterPath left;
        left.addRect(900, 500, 60, 100); // the bar's left half
        controller.setSelection(left);

        QPoint origin;
        const QImage copied = controller.copyPixels(&origin);
        QCOMPARE(copied.size(), QSize(50, 10)); // cropped to what is drawn
        QCOMPARE(origin, QPoint(910, 535));

        doc::Document &d = controller.documentForCommands(); // another layer, another frame
        auto ink = std::make_unique<doc::AddCelCommand>(d, d.celCount(), doc::CelProperties{"ink"});
        const doc::CelId inkId = ink->celId();
        controller.execute(std::move(ink));
        controller.setCurrentCel(inkId);
        controller.setCurrentFrame(5);
        const int cels = d.celCount();
        QVERIFY(controller.pasteImage(copied, origin));
        QCOMPARE(d.celCount(), cels); // no new layer
        QCOMPARE(controller.currentCel(), inkId);
        const doc::Cel &target = *controller.document().cel(inkId);
        QVERIFY(target.drawingAt(5));
        QVERIFY(!target.drawingAt(4)); // on this frame only
        QCOMPARE(target.drawingAt(5)->layers.front().image.pixel(930, 540), doc::Pixel(0xff00ff00));
        QCOMPARE(qAlpha(target.drawingAt(5)->layers.front().image.pixel(990, 540)), 0);
        QCOMPARE(controller.selection().boundingRect(), QRectF(910, 535, 50, 10));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo paste"));
        controller.undo(); // all of it: the drawing it made, the selection
        QVERIFY(!target.drawingAt(5));
        QCOMPARE(controller.selection().boundingRect(), QRectF(900, 500, 60, 100));

        QVERIFY(controller.pasteImage(QImage(20, 20, QImage::Format_ARGB32_Premultiplied), std::nullopt));
        QCOMPARE(controller.selection().boundingRect(), QRectF(950, 530, 20, 20)); // no place: centered

        doc::CelProperties locked = target.properties(); // a layer that cannot take it
        locked.locked = true;
        controller.execute(doc::makeSetCelProperties(controller.document(), inkId, locked));
        QString reason;
        QVERIFY(!controller.pasteImage(copied, origin, &reason));
        QVERIFY(!reason.isEmpty());

        auto empty = std::make_unique<doc::AddCelCommand>(d, d.celCount(), doc::CelProperties{"empty"});
        const doc::CelId emptyId = empty->celId();
        controller.execute(std::move(empty));
        controller.setCurrentCel(emptyId); // nothing to copy there
        reason.clear();
        QVERIFY(controller.copyPixels(nullptr, &reason).isNull());
        QVERIFY(!reason.isEmpty());
    }


    // Ctrl+C / Ctrl+X / Ctrl+V go through the system clipboard.
    void clipboardCopyCutPaste()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller->importImage(image, QStringLiteral("bar"));
        const doc::CelId bar = controller->currentCel();
        QPainterPath left;
        left.addRect(900, 500, 60, 100);
        controller->setSelection(left);
        auto action = [&](QKeySequence::StandardKey key) {
            for (QAction *a : window.findChildren<QAction *>())
                if (a->shortcuts().contains(QKeySequence::keyBindings(key).value(0)))
                    return a;
            return static_cast<QAction *>(nullptr);
        };
        QVERIFY(action(QKeySequence::Copy) && action(QKeySequence::Cut) && action(QKeySequence::Paste));

        action(QKeySequence::Cut)->trigger();
        const doc::Cel &source = *controller->document().cel(bar);
        QCOMPARE(qAlpha(source.drawingAt(0)->layers.front().image.pixel(930, 540)), 0); // cut
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage()); // for other apps too
        action(QKeySequence::Paste)->trigger(); // back onto the same layer, in place
        QCOMPARE(controller->currentCel(), bar);
        QCOMPARE(source.drawingAt(0)->layers.front().image.pixel(930, 540), doc::Pixel(0xff00ff00));

        QImage other(10, 10, QImage::Format_ARGB32); // an image from another app: centered
        other.fill(Qt::blue);
        QGuiApplication::clipboard()->setImage(other);
        action(QKeySequence::Paste)->trigger();
        QCOMPARE(source.drawingAt(0)->layers.front().image.pixel(960, 540), doc::Pixel(0xff0000ff));
    }


    // The ants march while there is a selection: the dashes swap in turns.
    void selectionAntsMarch()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(400, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::LassoSelect);

        QTest::mousePress(&canvas, Qt::LeftButton, {}, QPoint(100, 100));
        QTest::mouseMove(&canvas, QPoint(250, 100));
        QTest::mouseMove(&canvas, QPoint(250, 220));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, QPoint(250, 220));
        QVERIFY(controller.hasSelection());
        const int phase = canvas.selectionAntsPhase();
        QTRY_VERIFY(canvas.selectionAntsPhase() != phase);
        QTRY_VERIFY(canvas.selectionAntsPhase() == phase);

        const canvas::ViewTransform view; // phase 1 is phase 0 with black and white swapped
        QImage still(100, 100, QImage::Format_ARGB32), swapped(100, 100, QImage::Format_ARGB32);
        QPainterPath line;
        line.moveTo(10, 50);
        line.lineTo(90, 50);
        for (auto [image, ph] : {std::pair{&still, 0}, std::pair{&swapped, 1}}) {
            image->fill(Qt::transparent);
            QPainter p(image);
            canvas::paintSelectionOutline(p, view, line, ph);
        }
        const int y = view.documentToView().map(QPointF(0, 50)).toPoint().y();
        int swaps = 0;
        for (int x = 0; x < 100; ++x)
            if (qAlpha(still.pixel(x, y)) && still.pixel(x, y) != swapped.pixel(x, y))
                ++swaps;
        QVERIFY(swaps > 0);
    }


    // Making, changing and clearing a selection are undo steps.
    // Erase, cut and copy work on what is selected, each as one undo step.
    void selectionCommandsWorkOnTheSelectedPixels()
    {
        document::DocumentController controller;
        QImage image(100, 10, QImage::Format_ARGB32);
        image.fill(Qt::green);
        controller.importImage(image, QStringLiteral("bar")); // x 910..1009, y 535..544
        const doc::CelId bar = controller.currentCel();
        auto pixel = [&](doc::CelId id, int x, int y) {
            const doc::Cel &cel = *controller.document().cel(id);
            const doc::Drawing *drawing = cel.drawingAt(0);
            return drawing ? drawing->layers.front().image.pixel(x, y) : doc::Pixel(0);
        };
        const doc::Pixel green = pixel(bar, 930, 540);
        QString reason;
        QVERIFY(!controller.eraseSelection(&reason)); // nothing selected
        QVERIFY(!reason.isEmpty());

        QPainterPath left;
        left.addRect(900, 500, 60, 100); // the bar's left half
        controller.setSelection(left);
        QVERIFY(controller.eraseSelection());
        QCOMPARE(qAlpha(pixel(bar, 930, 540)), 0);
        QCOMPARE(pixel(bar, 990, 540), green);
        QVERIFY(controller.hasSelection());
        QCOMPARE(controller.undoText(), QStringLiteral("Undo clear"));
        controller.undo();
        QCOMPARE(pixel(bar, 930, 540), green);

        // Cut and copy go to a new layer above, in the same drawing.
        const doc::Drawing &drawing = *controller.document().cel(bar)->drawingAt(0);
        const doc::LayerId source = controller.currentLayer();
        auto layerPixel = [&](doc::LayerId id, int x, int y) {
            const doc::Layer *layer = drawing.layer(id);
            return layer ? layer->image.pixel(x, y) : doc::Pixel(0);
        };
        const int cels = controller.document().celCount();
        QVERIFY(controller.copySelectionToLayer());
        QCOMPARE(controller.document().celCount(), cels);
        QCOMPARE(drawing.layers.size(), std::size_t(2));
        const doc::LayerId copy = controller.currentLayer();
        QCOMPARE(controller.currentCel(), bar);
        QCOMPARE(drawing.indexOf(copy), drawing.indexOf(source) + 1);
        QCOMPARE(drawing.layer(copy)->properties.name, std::string("Layer 1 (copy)"));
        QCOMPARE(layerPixel(copy, 930, 540), green);
        QCOMPARE(qAlpha(layerPixel(copy, 990, 540)), 0);
        QCOMPARE(layerPixel(source, 930, 540), green); // the source keeps it
        controller.undo();
        QCOMPARE(drawing.layers.size(), std::size_t(1));
        QCOMPARE(controller.currentLayer(), source);

        QVERIFY(controller.cutSelectionToLayer());
        const doc::LayerId cut = controller.currentLayer();
        QCOMPARE(layerPixel(cut, 930, 540), green);
        QCOMPARE(qAlpha(layerPixel(source, 930, 540)), 0); // the source loses it
        QCOMPARE(controller.undoText(), QStringLiteral("Undo cut to layer"));
        controller.undo();
        QCOMPARE(drawing.layers.size(), std::size_t(1));
        QCOMPARE(layerPixel(source, 930, 540), green);

        QPainterPath empty; // nothing under the selection
        empty.addRect(100, 100, 50, 50);
        controller.setSelection(empty);
        QVERIFY(!controller.cutSelectionToLayer(&reason));
        QCOMPARE(drawing.layers.size(), std::size_t(1));
    }


    void invertSelectionSelectsTheRest()
    {
        document::DocumentController controller;
        controller.invertSelection(); // nothing selected: all
        QVERIFY(controller.selection().contains(QPointF(5, 5)));
        QPainterPath box;
        box.addRect(100, 100, 50, 50);
        controller.setSelection(box);
        controller.invertSelection();
        QVERIFY(!controller.selection().contains(QPointF(120, 120)));
        QVERIFY(controller.selection().contains(QPointF(5, 5)));
        controller.undo();
        QVERIFY(controller.selection().contains(QPointF(120, 120)));
    }


    // The bar shows with a selection, just under it, and its buttons act.
    void selectionBarFollowsTheSelection()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(600, 400);
        ui::SelectionBar bar(&canvas, &controller, &state);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QVERIFY(!bar.isVisible());

        QPainterPath box;
        box.addRect(800, 400, 300, 200);
        controller.setSelection(box);
        QVERIFY(bar.isVisible());
        const QRect area = canvas.view().documentToView().mapRect(box.boundingRect()).toAlignedRect();
        QVERIFY(bar.geometry().top() > area.bottom());
        QVERIFY(bar.geometry().top() - area.bottom() < 20);
        QVERIFY(canvas.rect().contains(bar.geometry()));

        canvas.zoomIn(); // follows the view
        const QRect zoomed = canvas.view().documentToView().mapRect(box.boundingRect()).toAlignedRect();
        QVERIFY(bar.geometry().top() > zoomed.bottom() || bar.geometry().bottom() < zoomed.top()
                || bar.geometry().bottom() == canvas.height() - 4);

        auto *invert = bar.findChild<QAbstractButton *>(QStringLiteral("invert"));
        QVERIFY(invert);
        // A pen on a button stays with the bar: the brush must not paint under it.
        state.setActiveTool(tools::ToolId::Brush);
        const QPointF at = QPointF(invert->rect().center());
        for (QEvent::Type type : {QEvent::TabletPress, QEvent::TabletMove, QEvent::TabletRelease}) {
            QTabletEvent pen(type, QPointingDevice::primaryPointingDevice(), at, invert->mapToGlobal(at), 0.8, 0, 0,
                             0, 0, 0, Qt::NoModifier, Qt::LeftButton,
                             type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(invert, &pen);
        }
        QCOMPARE(controller.undoText(), QStringLiteral("Undo selection")); // no stroke
        invert->click();
        QVERIFY(!controller.selection().contains(QPointF(900, 500)));
        bar.findChild<QAbstractButton *>(QStringLiteral("transform"))->click();
        QCOMPARE(state.activeTool(), tools::ToolId::Transform);
        state.setActiveTool(tools::ToolId::Brush);
        bar.findChild<QAbstractButton *>(QStringLiteral("deselect"))->click();
        QVERIFY(!controller.hasSelection());
        QVERIFY(!bar.isVisible());
    }


    void selectionChangesAreUndoable()
    {
        document::DocumentController controller;
        QPainterPath box;
        box.addRect(100, 100, 50, 50);
        controller.setSelection(box);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo selection"));
        controller.selectAll();
        controller.clearSelection();
        QVERIFY(!controller.hasSelection());

        controller.undo();
        QCOMPARE(controller.selection().boundingRect(),
                 QRectF(0, 0, controller.document().width(), controller.document().height()));
        controller.undo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(100, 100, 50, 50));
        controller.undo();
        QVERIFY(!controller.hasSelection());
        QVERIFY(!controller.canUndo());
        controller.redo();
        QCOMPARE(controller.selection().boundingRect(), QRectF(100, 100, 50, 50));

        controller.setSelection(box); // the same selection again is no step
        controller.clearSelection();
        controller.undo();
        QVERIFY(controller.canUndo());
        controller.undo();
        QVERIFY(!controller.canUndo());
    }


    // While a transform is open a bar with OK and Cancel floats under its box (the selection's
    // bar gives way); OK applies it, Cancel drops it, either returns to the previous tool.
    void transformBarAppliesOrDrops()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *state = window.findChild<tools::ToolState *>();
        auto *canvas = window.findChild<canvas::CanvasView *>();
        auto *bar = window.findChild<ui::TransformBar *>();
        auto *selectionBar = window.findChild<ui::SelectionBar *>();
        QVERIFY(bar && selectionBar);
        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller->importImage(image, QStringLiteral("square")); // 910..1009, 490..589
        QPainterPath area;
        area.addRect(900, 480, 130, 130); // a selection larger than the pixels
        controller->setSelection(area);
        QVERIFY(selectionBar->isVisible());
        QVERIFY(!bar->isVisible());

        state->setActiveTool(tools::ToolId::Transform);
        QCoreApplication::processEvents();
        QCOMPARE(canvas->transformBounds(), QRectF(900, 480, 130, 130)); // the selection's box
        QVERIFY(bar->isVisible());
        QVERIFY(!selectionBar->isVisible());
        const QRect box = canvas->view().documentToView().mapRect(canvas->transformBounds()).toAlignedRect();
        QVERIFY(bar->geometry().top() >= box.bottom() || bar->geometry().bottom() <= box.top()); // not on the box
        auto *ok = bar->findChild<QToolButton *>(QStringLiteral("ok"));
        auto *cancel = bar->findChild<QToolButton *>(QStringLiteral("cancel"));
        QVERIFY(ok && cancel);
        QCOMPARE(ok->text(), QStringLiteral("OK"));
        QCOMPARE(cancel->text(), QStringLiteral("Cancel"));

        auto at = [&](QPointF doc) { return canvas->view().documentToView().map(doc).toPoint(); };
        auto drag = [&](QPointF from, QPointF to) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, at(from));
            QTest::mouseMove(canvas, at(to));
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, at(to));
        };
        const doc::Cel &cel = *controller->document().cel(controller->currentCel());
        const doc::TiledImage &pixels = cel.drawingAt(0)->layers.front().image;
        drag({965, 545}, {1065, 545}); // moved 100 to the right
        QCOMPARE(pixels.pixel(1060, 545), doc::Pixel(0xffff0000));
        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(!bar->isVisible());
        QVERIFY(selectionBar->isVisible());
        QVERIFY(state->activeTool() != tools::ToolId::Transform);
        QCOMPARE(qAlpha(pixels.pixel(1060, 545)), 0); // dropped
        QCOMPARE(pixels.pixel(960, 545), doc::Pixel(0xffff0000));

        state->setActiveTool(tools::ToolId::Transform);
        QVERIFY(bar->isVisible());
        drag({965, 545}, {1065, 545});
        QTest::mouseClick(ok, Qt::LeftButton);
        QVERIFY(!bar->isVisible());
        QVERIFY(state->activeTool() != tools::ToolId::Transform);
        QCOMPARE(pixels.pixel(1060, 545), doc::Pixel(0xffff0000)); // applied
        QCOMPARE(controller->undoText(), QStringLiteral("Undo transform"));
    }


    // The same for a transform; and Ctrl+Z in the window goes there first.
    void transformUndoGoesThroughItsDrags()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *state = window.findChild<tools::ToolState *>();
        auto *canvas = window.findChild<canvas::CanvasView *>();
        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller->importImage(image, QStringLiteral("square")); // 910..1009, 490..589
        state->setActiveTool(tools::ToolId::Transform);
        auto at = [&](QPointF doc) { return canvas->view().documentToView().map(doc).toPoint(); };
        auto drag = [&](QPointF from, QPointF to) {
            QTest::mousePress(canvas, Qt::LeftButton, {}, at(from));
            QTest::mouseMove(canvas, at(to));
            QTest::mouseRelease(canvas, Qt::LeftButton, {}, at(to));
        };
        drag({960, 540}, {1060, 540});
        drag({1060, 540}, {1060, 640});
        QAction *undo = nullptr;
        for (QAction *a : window.findChildren<QAction *>())
            if (a->shortcuts().contains(QKeySequence::keyBindings(QKeySequence::Undo).value(0)))
                undo = a;
        QVERIFY(undo);
        QCOMPARE(undo->text(), QStringLiteral("Undo step"));
        undo->trigger();
        QCOMPARE(state->activeTool(), tools::ToolId::Transform); // still transforming
        const doc::Cel &cel = *controller->document().cel(controller->currentCel());
        const doc::TiledImage &pixels = cel.drawingAt(0)->layers.front().image;
        QCOMPARE(pixels.pixel(1060, 540), doc::Pixel(0xffff0000)); // back after the first drag
        QCOMPARE(qAlpha(pixels.pixel(1060, 640)), 0);
        undo->trigger();
        QCOMPARE(pixels.pixel(960, 540), doc::Pixel(0xffff0000)); // as it was
        QCOMPARE(undo->text(), QStringLiteral("Undo import image"));
    }


    // Ctrl+C and Ctrl+V copy the drawing's pixels from the timeline too (the focus often stays there after a
    // frame change): with no block selected Ctrl+C is the window's copy, and Ctrl+V after it pastes the pixels at the
    // current frame, not the blocks (nothing copied: an error sound).
    void pastePixelsWithTheTimelineFocused()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        auto *controller = window.findChild<document::DocumentController *>();
        auto *timeline = window.findChild<ui::TimelineView *>();
        QVERIFY(timeline);
        QImage image(60, 60, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller->importImage(image, QStringLiteral("a")); // 930..989, 510..569
        const doc::CelId cel = controller->currentCel();
        controller->insertFrame(cel, 1, false);
        controller->setCurrentFrame(0);
        controller->selectAll();
        timeline->setFocus();
        QVERIFY(timeline->selectedFrames().empty());
        QTest::keyClick(timeline, Qt::Key_C, Qt::ControlModifier); // the window's copy: the pixels
        controller->clearSelection();
        controller->setCurrentFrame(1);
        QTest::keyClick(timeline, Qt::Key_V, Qt::ControlModifier);
        const doc::Cel *c = controller->document().cel(cel);
        const doc::Drawing *second = c->drawingAt(1);
        QVERIFY(second);
        QCOMPARE(qAlpha(second->layers.front().image.pixel(960, 540)), 255);
    }


    // With deferred rendering, the samples of a drag are drawn once per turn of the event loop at the latest
    // transform (the box follows at once), and the release draws what is pending.
    void transformDrawsOncePerTurnWhileDragging()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(100, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("square")); // 910..1009, 490..589
        int repaints = 0;
        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [&] { ++repaints; });
        tool.setDeferredRendering(true);
        tool.setViewZoom(1.0);
        tool.activated();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto alpha = [&](int x, int y) { return qAlpha(cel.drawingAt(0)->layers.front().image.pixel(x, y)); };
        QVERIFY(alpha(960, 540) > 0);
        QCOMPARE(alpha(1100, 540), 0);

        tool.press({960, 540}, Qt::NoModifier); // inside: moves
        tool.move({1000, 540}, Qt::NoModifier);
        tool.move({1050, 540}, Qt::NoModifier);
        tool.move({1100, 540}, Qt::NoModifier); // three samples, nothing drawn yet
        QCOMPARE(tool.transform().dx(), 140.0); // the box is already there
        QCOMPARE(alpha(1100, 540), 0);
        QCoreApplication::processEvents(); // the turn: once, at the last
        QVERIFY(alpha(1100, 540) > 0);
        QCOMPARE(alpha(960, 540), 0);

        tool.move({1200, 540}, Qt::NoModifier); // pending when the button goes up
        tool.release({1200, 540}, Qt::NoModifier);
        QVERIFY(alpha(1200, 540) > 0); // drawn at the release, not left for later
        QCOMPARE(alpha(1100, 540), 0);
        tool.cancel();
    }


    // A transform of a whole cel (every frame's layers chosen): while it is dragged only the frame shown is
    // drawn, the others when it is applied, and all of it is one undo step.
    void transformOfAWholeCelDrawsTheOtherFramesWhenApplied()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QList<QImage> images;
        for (QColor color : {QColor(Qt::red), QColor(Qt::green), QColor(Qt::blue)}) {
            QImage image(100, 100, QImage::Format_ARGB32);
            image.fill(color);
            images << image;
        }
        QCOMPARE(controller.importImageSequence(images, QStringLiteral("seq"), 0, 1), 3); // 910..1009, 490..589
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        std::vector<document::DocumentController::LayerRef> refs;
        for (const auto &[id, drawing] : cel.drawings())
            for (const doc::Layer &layer : drawing.layers)
                refs.push_back({cel.id(), id, layer.id});
        controller.setLayerSelection(refs);
        controller.setCurrentFrame(0);
        auto alphaAt = [&](int frame, int x, int y) {
            const doc::Drawing *d = cel.drawingAt(frame);
            return d ? qAlpha(d->layers.front().image.pixel(x, y)) : -1;
        };

        canvas::TransformTool tool(&controller, &state, [](const QString &) {}, [] {});
        tool.setViewZoom(1.0);
        tool.activated();
        QVERIFY(tool.hasPendingEdit());
        tool.press({960, 540}, Qt::NoModifier); // inside the box: moves it
        tool.move({1160, 540}, Qt::NoModifier); // 200 px to the right
        tool.release({1160, 540}, Qt::NoModifier);
        QVERIFY(alphaAt(0, 1160, 540) > 0 && alphaAt(0, 960, 540) == 0); // the frame shown: moved
        QVERIFY(alphaAt(1, 960, 540) > 0 && alphaAt(1, 1160, 540) == 0); // the others: where they were
        QVERIFY(alphaAt(2, 960, 540) > 0);

        tool.commit(); // applied: every frame
        QVERIFY(alphaAt(1, 1160, 540) > 0 && alphaAt(1, 960, 540) == 0);
        QVERIFY(alphaAt(2, 1160, 540) > 0 && alphaAt(2, 960, 540) == 0);
        controller.undo(); // one step for all of it
        for (int frame : {0, 1, 2})
            QVERIFY(alphaAt(frame, 960, 540) > 0 && alphaAt(frame, 1160, 540) == 0);
    }

};

QTEST_MAIN(TestUiTools)
#include "tst_ui_tools.moc"
