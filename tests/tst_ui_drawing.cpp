// Tests for the UI layer: the drawing tools: shapes, effect brushes, perspective, straight lines.

#include "test_common.h"

class TestUiDrawing : public QObject
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


    // The eyedropper picks the color shown; Alt does the same with the brush,
    // without painting.
    void eyedropperPicksTheShownColor()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(200, 200, QImage::Format_ARGB32);
        image.fill(QColor(200, 40, 30));
        controller.importImage(image, QStringLiteral("red")); // centered: 860..1059, 440..639
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(400, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        const QPoint onRed = canvas.view().documentToView().map(QPointF(960, 540)).toPoint();
        const QPoint onPaper = canvas.view().documentToView().map(QPointF(100, 100)).toPoint();

        state.setActiveTool(tools::ToolId::Eyedropper);
        QTest::mousePress(&canvas, Qt::LeftButton, {}, onRed);
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, onRed);
        QCOMPARE(state.color(), QColor(200, 40, 30));

        state.setActiveTool(tools::ToolId::Brush);
        state.setColor(Qt::blue);
        QTest::keyPress(&canvas, Qt::Key_Alt);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::AltModifier, onRed);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier, onRed); // a click: the color under it
        QTest::keyRelease(&canvas, Qt::Key_Alt);
        QCOMPARE(state.color(), QColor(200, 40, 30));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo import image")); // no stroke
    }

    // Alt and the pen dragged sideways turn the frames over (a frame for each 20 pixels), without painting or picking
    // a color; a click that does not move still picks.
    void altAndADragFlipTheFrames()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.setSceneLength(30);
        QImage image(200, 200, QImage::Format_ARGB32);
        image.fill(QColor(200, 40, 30));
        controller.importImage(image, QStringLiteral("red"));
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(500, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush);
        state.setColor(Qt::blue);
        const QString undoBefore = controller.undoText();
        controller.setCurrentFrame(10);
        const QPoint start(200, 150);

        QTest::keyPress(&canvas, Qt::Key_Alt);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::AltModifier, start);
        QTest::mouseMove(&canvas, start + QPoint(4, 0)); // not far enough to be a drag
        QCOMPARE(controller.currentFrame(), 10);
        QTest::mouseMove(&canvas, start + QPoint(25, 0));
        QCOMPARE(controller.currentFrame(), 11);
        QTest::mouseMove(&canvas, start + QPoint(65, 3));
        QCOMPARE(controller.currentFrame(), 13);
        QTest::mouseMove(&canvas, start + QPoint(-45, 0)); // and back, past where it began
        QCOMPARE(controller.currentFrame(), 8);
        QTest::mouseMove(&canvas, start + QPoint(-400, 0)); // the first frame is the end of the way
        QCOMPARE(controller.currentFrame(), 0);
        QTest::mouseMove(&canvas, start + QPoint(2000, 0)); // and the last
        QCOMPARE(controller.currentFrame(), 29);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier, start + QPoint(2000, 0));
        QTest::keyRelease(&canvas, Qt::Key_Alt);
        QCOMPARE(controller.currentFrame(), 29); // it stays where the drag ended
        QCOMPARE(state.color(), QColor(Qt::blue));    // no color was picked
        QCOMPARE(controller.undoText(), undoBefore);  // and nothing was painted
    }


    // A mask holds what is hidden, in black, and is white (nothing there) where the layer shows: on it the eraser hides
    // (paints black) and the brush puts the layer back (takes the black away), whatever the color selected.
    void onAMaskTheEraserHidesAndTheBrushShows()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage red(300, 300, QImage::Format_ARGB32);
        red.fill(Qt::red);
        controller.importImage(red, QStringLiteral("x")); // centered on the 1920x1080 page
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(500, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QVERIFY(controller.createLayerMask());
        const doc::Layer &layer = controller.currentDrawing()->layers.front();
        QVERIFY(layer.mask && layer.mask->isEmpty()); // starts white: everything shows

        const QTransform toView = canvas.view().documentToView();
        const QPoint start = toView.map(QPointF(900, 540)).toPoint();
        auto stroke = [&](tools::ToolId tool) {
            state.setActiveTool(tool);
            QTest::mousePress(&canvas, Qt::LeftButton, {}, start);
            for (int i = 1; i <= 20; ++i)
                QTest::mouseMove(&canvas, start + QPoint(i * 2, 0));
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, start + QPoint(40, 0));
        };
        const QPoint inside = toView.inverted().map(QPointF(start + QPoint(20, 0))).toPoint(); // on the stroke
        state.setColor(Qt::white); // the color does not matter

        stroke(tools::ToolId::Brush); // the brush on a mask with nothing hidden: nothing to take away
        QVERIFY(!layer.mask || layer.mask->pixel(inside.x(), inside.y()) == 0);

        stroke(tools::ToolId::Eraser); // the eraser hides
        const doc::Pixel hidden = layer.mask->pixel(inside.x(), inside.y());
        QVERIFY2((hidden >> 24) > 200 && (hidden & 0xffffff) == 0, "black");

        state.setColor(Qt::black);
        stroke(tools::ToolId::Brush); // the brush shows it again
        QVERIFY((layer.mask->pixel(inside.x(), inside.y()) >> 24) < 40);
        QCOMPARE(layer.image.pixel(inside.x(), inside.y()), doc::Pixel(0xffff0000)); // the layer itself is untouched
    }


    // The vector button of the Layers panel adds a vector layer, with a name of its own.
    void theVectorButtonAddsAVectorLayer()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 800);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, controller.currentCel(), 0, 1, "Line").command);
        QCoreApplication::processEvents();
        auto *add = panel.findChild<QToolButton *>(QStringLiteral("addVector"));
        QVERIFY(add && add->isEnabled());
        add->click();
        add->click();
        QCoreApplication::processEvents();
        const doc::Drawing &drawing = *controller.currentDrawing();
        QCOMPARE(drawing.layers.size(), std::size_t(3));
        QVERIFY(drawing.layers[1].kind == doc::LayerKind::Vector && drawing.layers[2].kind == doc::LayerKind::Vector);
        QCOMPARE(drawing.layers[1].properties.name, std::string("Vector 1"));
        QCOMPARE(drawing.layers[2].properties.name, std::string("Vector 2"));
    }


    // The brush on a vector layer paints its pixels and keeps the stroke (its points and how to draw them again), both
    // in one undo step; the eraser keeps its strokes too, marked as erasing. The tools that work on pixels are refused.
    void aStrokeOnAVectorLayerIsKept()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(500, 400);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, controller.currentCel(), 0, 4, "Line").command);
        QVERIFY(controller.addLayer(true, doc::LayerKind::Vector));
        QCoreApplication::processEvents();
        auto layer = [&]() -> const doc::Layer & { return *controller.currentDrawing()->layer(controller.currentLayer()); };
        QVERIFY(layer().kind == doc::LayerKind::Vector);
        QCOMPARE(layer().properties.name, std::string("Vector 1"));
        QVERIFY(layer().strokes.empty() && layer().image.isEmpty());

        state.setColor(QColor(10, 120, 200));
        const QTransform toView = canvas.view().documentToView();
        const QPoint start = toView.map(QPointF(900, 540)).toPoint();
        auto stroke = [&](tools::ToolId tool) {
            state.setActiveTool(tool);
            QTest::mousePress(&canvas, Qt::LeftButton, {}, start);
            for (int i = 1; i <= 20; ++i)
                QTest::mouseMove(&canvas, start + QPoint(i * 2, i));
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, start + QPoint(40, 20));
        };
        stroke(tools::ToolId::Brush);
        QCOMPARE(layer().strokes.size(), std::size_t(1));
        const doc::VectorStroke &kept = layer().strokes.front();
        QVERIFY(kept.points.size() >= 10);
        QVERIFY(!kept.erase);
        QCOMPARE(QColor::fromRgba(kept.color), QColor(10, 120, 200));
        QCOMPARE(kept.size, state.settings(tools::ToolId::Brush).size);
        QVERIFY(!kept.brush.empty());
        QVERIFY(!layer().image.isEmpty()); // the pixels are there too, to be shown
        const doc::TileCoord tile = layer().image.tileCoords().front();

        stroke(tools::ToolId::Eraser);
        QCOMPARE(layer().strokes.size(), std::size_t(2));
        QVERIFY(layer().strokes.back().erase);

        controller.undo(); // the eraser stroke and the pixels it took, together
        QCOMPARE(layer().strokes.size(), std::size_t(1));
        controller.undo();
        QVERIFY(layer().strokes.empty() && layer().image.isEmpty());
        controller.redo();
        QCOMPARE(layer().strokes.size(), std::size_t(1));
        QVERIFY(layer().image.tile(tile));

        // Not the tools that read or move the pixels: they would leave the strokes behind.
        QString reason;
        QVERIFY(!controller.beginPaint(true, &reason));
        QVERIFY(reason.contains(QStringLiteral("vector")));
        QVERIFY(!controller.clearCurrentLayer(&reason));
        QVERIFY(reason.contains(QStringLiteral("vector")));
        QCOMPARE(layer().strokes.size(), std::size_t(1));
    }


    // A stroke on an empty frame that leaves nothing (the selection is elsewhere) takes back the frame it made when it
    // ends: nothing may look at the image that went with it afterwards (the line start was read from it: a crash).
    void aStrokeThatPaintsNothingOnAnEmptyFrame()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(400, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush);
        QPainterPath elsewhere;
        elsewhere.addRect(0, 0, 10, 10);
        controller.setSelection(elsewhere, true); // the pen paints only inside it
        QVERIFY(!controller.currentDrawing());
        const QPoint p = canvas.view().documentToView().map(QPointF(960, 540)).toPoint();
        QTest::mousePress(&canvas, Qt::LeftButton, {}, p);
        for (int i = 1; i <= 10; ++i)
            QTest::mouseMove(&canvas, p + QPoint(i * 6, i * 2));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, p + QPoint(60, 20));
        QCoreApplication::processEvents();
        QVERIFY(!controller.currentDrawing()); // the empty frame stays empty
    }


    // With a cel or a frame selected (or the camera), the pixel tools show the "not allowed" cursor, not the brush.
    void cursorSaysNoWhenNothingCanBeDrawnOn()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(20, 20, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("red")); // the frame has a drawing
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(400, 300);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));

        state.setActiveTool(tools::ToolId::Brush);
        controller.setLayerChosen(true);
        QVERIFY(!controller.drawingBlocked());
        QVERIFY(canvas.cursor().shape() != Qt::ForbiddenCursor);

        controller.setLayerChosen(false); // a cel or a frame is selected
        QVERIFY(controller.drawingBlocked());
        QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor);
        state.setActiveTool(tools::ToolId::Fill);
        QCOMPARE(canvas.cursor().shape(), Qt::ForbiddenCursor);
        state.setActiveTool(tools::ToolId::Hand); // moving the view is always fine
        QVERIFY(canvas.cursor().shape() != Qt::ForbiddenCursor);
        state.setActiveTool(tools::ToolId::RectSelect);
        QVERIFY(canvas.cursor().shape() != Qt::ForbiddenCursor);

        state.setActiveTool(tools::ToolId::Brush);
        controller.setLayerChosen(true);
        QVERIFY(canvas.cursor().shape() != Qt::ForbiddenCursor); // back to the brush

        // An empty frame is drawn on even so: drawing makes the drawing there.
        controller.setLayerChosen(false);
        controller.setCurrentFrame(controller.document().frameCount() + 10);
        QVERIFY(!controller.drawingBlocked());
    }


    // The perspective button adds a perspective layer: a grid that cannot be
    // drawn on, and that the panel has no controls for (it is set on the canvas).
    void perspectiveLayers()
    {
        document::DocumentController controller;
        ui::LayersPanel panel(&controller);
        panel.resize(320, 800);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, controller.currentCel(), 0, 1, "Line").command);
        QCoreApplication::processEvents();

        auto *add = panel.findChild<QToolButton *>(QStringLiteral("addPerspective"));
        QVERIFY(add && add->isEnabled());
        add->click();
        QCoreApplication::processEvents();
        const doc::Drawing &drawing = *controller.currentDrawing();
        const doc::Layer *grid = drawing.layer(controller.currentLayer());
        QVERIFY(grid && grid->kind == doc::LayerKind::Perspective);
        QCOMPARE(grid->properties.name, std::string("Perspective 1"));
        QCOMPARE(grid->properties.perspective.originX, d.width() / 2.0); // fit to the document
        QCOMPARE(grid->properties.perspective.position, perspective::Vec3());
        QVERIFY(!panel.findChild<QSlider *>(QStringLiteral("perspectiveTilt"))); // no controls in the panel

        // Not drawn on, nor worked on with the selection tools.
        QString reason;
        QVERIFY(!controller.beginPaint(true, &reason));
        QVERIFY(reason.contains(QStringLiteral("perspective")));
        controller.selectAll();
        QVERIFY(!controller.eraseSelection(&reason));
        QVERIFY(reason.contains(QStringLiteral("perspective")));
    }


    // The handles of a perspective layer on the canvas, whatever the tool: the
    // middle square moves the plane, the arrows move it along its axes, the
    // rings turn it around them, the corner sizes the cells, the button opens
    // the settings. Each drag is live and one undo step.
    void transparentColorErasesWithTheBrush()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setColor(Qt::red);
        state.setSize(tools::ToolId::Brush, 24.0);
        auto stroke = [&](double y) {
            const QTransform toView = canvas.view().documentToView();
            const QPoint a = toView.map(QPointF(100, y)).toPoint(), b = toView.map(QPointF(300, y)).toPoint();
            QTest::mousePress(&canvas, Qt::LeftButton, {}, a);
            QTest::mouseMove(&canvas, (a + b) / 2);
            QTest::mouseMove(&canvas, b);
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, b);
        };
        auto alpha = [&](int x, int y) {
            return qAlpha(controller.currentDrawing()->layers.front().image.pixel(x, y));
        };
        stroke(200);
        QVERIFY(alpha(200, 200) > 0);

        // The panel's bar selects it; the wheel's colors (and the swap) select the main color back.
        ui::ColorWheelPanel panel(&state);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *swatches = panel.findChild<ui::ColorSwatchPair *>();
        QVERIFY(swatches);
        QVERIFY(!state.transparentSelected());
        QTest::mouseClick(swatches, Qt::LeftButton, {}, QPoint(swatches->width() / 2, swatches->height() - 4));
        QVERIFY(state.transparentSelected());

        // The brush now erases (its tip, its size), and stays erasing stroke after stroke.
        stroke(200);
        QCOMPARE(alpha(200, 200), 0);
        QCOMPARE(state.activeTool(), tools::ToolId::Brush);
        stroke(300);
        QVERIFY(controller.currentDrawing()->layers.front().image.pixel(200, 300) == 0);

        state.setColor(Qt::red); // choosing a color, even the same one
        QVERIFY(!state.transparentSelected());
        stroke(300);
        QVERIFY(alpha(200, 300) > 0);
        state.setTransparentSelected(true);
        state.switchColor();
        QVERIFY(!state.transparentSelected());
    }


    // The camera selected in the Layers panel is moved, zoomed and turned on the canvas with its frame and handles;
    // what is changed at the current frame is the camera's key there, and a drag is one undo step.
    void cameraGizmoMovesZoomsAndTurnsTheCamera()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(1000, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        using Handle = canvas::CameraGizmo::Handle;
        const canvas::CameraGizmo &gizmo = canvas.cameraGizmo();
        const double zoom = canvas.view().zoom();
        auto at = [&](Handle h) {
            const auto p = gizmo.handlePoint(h, canvas.view());
            return p ? canvas.view().documentToView().map(*p) : QPointF(-1000, -1000);
        };
        auto drag = [&](QPointF from, QPointF by) {
            QTest::mousePress(&canvas, Qt::LeftButton, {}, from.toPoint());
            QTest::mouseMove(&canvas, (from + by * 0.5).toPoint());
            QTest::mouseMove(&canvas, (from + by).toPoint());
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, (from + by).toPoint());
        };
        auto near = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };

        // No camera, or one that is not selected, has no handles.
        QCOMPARE(gizmo.handleAt(QPointF(100, 100), canvas.view()), Handle::None);
        controller.addCamera();
        QVERIFY(!gizmo.editable());
        QCOMPARE(gizmo.handleAt(QPointF(100, 100), canvas.view()), Handle::None);
        controller.selectCamera(true);
        QVERIFY(gizmo.editable());
        for (Handle h : {Handle::Move, Handle::Scale, Handle::Rotate}) {
            const auto p = gizmo.handlePoint(h, canvas.view());
            QVERIFY2(p, qPrintable(QStringLiteral("handle %1 shown").arg(int(h))));
            QCOMPARE(gizmo.handleAt(*p, canvas.view()), h);
        }
        const doc::CameraPose whole = controller.camera()->poseAt(0);
        const QString undoBefore = controller.undoText();

        // Inside the frame: the camera moves as far as the pen went (the brush is the tool: it draws nothing).
        QVERIFY(tools::ToolState::isPaintTool(state.activeTool()));
        drag(at(Handle::Move), QPointF(60, -30));
        QVERIFY(near(controller.camera()->poseAt(0).centerX, whole.centerX + 60.0 / zoom, 2.0 / zoom));
        QVERIFY(near(controller.camera()->poseAt(0).centerY, whole.centerY - 30.0 / zoom, 2.0 / zoom));
        QCOMPARE(controller.camera()->keys().size(), size_t(1)); // the key at frame 0 changed
        QCOMPARE(controller.undoText(), QStringLiteral("Undo camera"));
        QVERIFY(controller.document().celCount() == 0 || controller.currentDrawing() == nullptr
                || controller.currentDrawing()->layers.front().image.isEmpty());
        controller.undo(); // the whole drag at once
        QCOMPARE(controller.camera()->poseAt(0), whole);
        QCOMPARE(controller.undoText(), undoBefore);

        // At another frame, the same drag makes a key there; the first one stays.
        controller.setCurrentFrame(12);
        QVERIFY(controller.camera()->keyAt(12) == nullptr);
        drag(at(Handle::Move), QPointF(-80, 40));
        QCOMPARE(controller.camera()->keys().size(), size_t(2));
        QVERIFY(controller.camera()->keyAt(12));
        QVERIFY(near(controller.camera()->poseAt(12).centerX, whole.centerX - 80.0 / zoom, 2.0 / zoom));
        QCOMPARE(controller.camera()->poseAt(0), whole);
        controller.undo();
        QCOMPARE(controller.camera()->keys().size(), size_t(1));

        // A corner pulled out: the frame is bigger, the camera sees more, the zoom is less; pushed in, more.
        const QPointF center = at(Handle::Move);
        QPointF corner = at(Handle::Scale);
        QPointF out = (corner - center) * 0.5; // half as far again
        drag(corner, out);
        QVERIFY(near(controller.camera()->poseAt(12).zoom, 1.0 / 1.5, 0.03));
        QVERIFY(near(controller.camera()->poseAt(12).centerX, whole.centerX, 1.0)); // around its middle
        controller.undo();
        corner = at(Handle::Scale);
        drag(corner, (center - corner) * 0.5); // half way in
        QVERIFY(near(controller.camera()->poseAt(12).zoom, 2.0, 0.08));
        controller.undo();

        // The knob carried a quarter of a turn round the middle turns the camera that much, clockwise.
        const QPointF knob = at(Handle::Rotate);
        const QPointF arm = knob - center;
        const QPointF quarter(-arm.y(), arm.x()); // the arm a quarter turn on, clockwise on the screen (y down)
        drag(knob, quarter - arm);
        QVERIFY(near(controller.camera()->poseAt(12).rotation, 90.0, 3.0));
        controller.undo();
        // Round more than a turn: it goes on.
        QTest::mousePress(&canvas, Qt::LeftButton, {}, knob.toPoint());
        for (int i = 1; i <= 12; ++i) {
            const double a = i * 35.0 * 3.14159265358979 / 180.0; // 420 degrees in all
            const QPointF at2 = center + QPointF(arm.x() * std::cos(a) - arm.y() * std::sin(a),
                                                 arm.x() * std::sin(a) + arm.y() * std::cos(a));
            QTest::mouseMove(&canvas, at2.toPoint());
        }
        const double end = 420.0 * 3.14159265358979 / 180.0;
        const QPointF last = center + QPointF(arm.x() * std::cos(end) - arm.y() * std::sin(end),
                                              arm.x() * std::sin(end) + arm.y() * std::cos(end));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, last.toPoint());
        QVERIFY(near(controller.camera()->poseAt(12).rotation, 420.0, 4.0));
        controller.undo();

        // A press that moves nothing is no step.
        const QString text = controller.undoText();
        drag(at(Handle::Move), QPointF(0, 0));
        QCOMPARE(controller.undoText(), text);

        // Off: no handles, and the pen is the tool's again.
        controller.setCameraVisible(false);
        QVERIFY(!gizmo.editable());
        QCOMPARE(gizmo.handleAt(center, canvas.view()), Handle::None);
        controller.setCameraVisible(true);
        controller.selectCamera(false);
        QVERIFY(!gizmo.editable());
    }

    void perspectiveGizmo()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(1000, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        doc::Document &d = controller.documentForCommands();
        controller.execute(doc::makeNewDrawing(d, controller.currentCel(), 0, 1, "Line").command);
        using Handle = canvas::PerspectiveGizmo::Handle;
        const canvas::PerspectiveGizmo &gizmo = canvas.perspectiveGizmo();
        QVERIFY(!gizmo.target()); // a raster layer is current
        QVERIFY(controller.addLayer(false, doc::LayerKind::Perspective));
        QVERIFY(gizmo.target());
        const doc::Drawing &drawing = *controller.currentDrawing();
        const doc::LayerId id = controller.currentLayer();
        auto grid = [&] { return drawing.layer(id)->properties.perspective; };
        auto at = [&](Handle h) {
            const auto p = gizmo.handlePoint(h, canvas.view());
            return p ? canvas.view().documentToView().map(*p) : QPointF(-1000, -1000);
        };
        for (Handle h : {Handle::Move, Handle::AxisX, Handle::AxisY, Handle::AxisZ, Handle::RingX, Handle::RingY,
                         Handle::RingZ, Handle::Scale, Handle::Menu}) {
            const auto p = gizmo.handlePoint(h, canvas.view());
            QVERIFY2(p, qPrintable(QStringLiteral("handle %1 shown").arg(int(h))));
            QCOMPARE(gizmo.handleAt(*p, canvas.view()), h);
        }
        auto drag = [&](QPointF from, QPointF by) {
            QTest::mousePress(&canvas, Qt::LeftButton, {}, from.toPoint());
            QTest::mouseMove(&canvas, (from + by * 0.5).toPoint());
            QTest::mouseMove(&canvas, (from + by).toPoint());
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, (from + by).toPoint());
        };
        auto near = [](double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; };
        const double zoom = canvas.view().zoom();
        const QString undoBefore = controller.undoText();

        // The middle square: as far as the pointer went, on the sheet (the brush is the tool).
        QVERIFY(tools::ToolState::isPaintTool(state.activeTool()));
        drag(at(Handle::Move), QPointF(60, -30));
        QVERIFY(near(grid().position.x, 60.0 / zoom, 2.0 / zoom));
        QVERIFY(near(grid().position.y, -30.0 / zoom, 2.0 / zoom));
        QCOMPARE(grid().position.z, 0.0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo layer properties"));
        QVERIFY(drawing.layers.front().image.isEmpty()); // the brush drew nothing
        controller.undo();
        QCOMPARE(grid().position, perspective::Vec3());
        QCOMPARE(controller.undoText(), undoBefore);

        // The blue arrow: along the plane's normal, the orientation left alone.
        const perspective::Rotation orientation = grid().orientation;
        const QPointF blue = at(Handle::AxisZ) - at(Handle::Move);
        drag(at(Handle::AxisZ), blue / std::hypot(blue.x(), blue.y()) * 40.0);
        const perspective::Vec3 normal = perspective::axis(grid(), {0, 0, -1});
        QVERIFY(perspective::dot(grid().position, normal) > 1.0);
        const perspective::Vec3 rest = grid().position - normal * perspective::dot(grid().position, normal);
        QVERIFY(std::sqrt(perspective::dot(rest, rest)) < 1e-6); // only along it
        QCOMPARE(grid().orientation, orientation);
        controller.undo();

        // The blue ring: around the normal, which stays put; the grid's lines turn.
        const perspective::Vec3 xBefore = perspective::axis(grid(), {1, 0, 0});
        QPointF from = at(Handle::RingZ);
        drag(from, QPointF(25, 25));
        const perspective::Vec3 normalAfter = perspective::axis(grid(), {0, 0, -1});
        QVERIFY(perspective::dot(normalAfter, normal) > 1.0 - 1e-9);
        QVERIFY(perspective::dot(perspective::axis(grid(), {1, 0, 0}), xBefore) < 0.999);
        QCOMPARE(grid().position, perspective::Vec3());
        QCOMPARE(controller.undoText(), QStringLiteral("Undo layer properties"));
        controller.undo();
        QCOMPARE(grid().orientation, orientation);

        // The red ring: around the grid's x axis, which stays put.
        drag(at(Handle::RingX), QPointF(0, 30));
        QVERIFY(perspective::dot(perspective::axis(grid(), {1, 0, 0}), xBefore) > 1.0 - 1e-9);
        QVERIFY(perspective::dot(perspective::axis(grid(), {0, 0, -1}), normal) < 0.999);
        controller.undo();

        // The corner: away from the middle, bigger cells.
        const double cell = grid().cell;
        const QPointF out = at(Handle::Scale) - at(Handle::Move);
        drag(at(Handle::Scale), out * 0.5);
        QVERIFY(grid().cell > cell * 1.3);
        controller.undo();
        QCOMPARE(grid().cell, cell);

        // The settings menu: each choice an undo step.
        QMenu *menu = const_cast<canvas::PerspectiveGizmo &>(gizmo).createMenu(&canvas);
        auto find = [](QMenu *m, const QString &text) -> QAction * {
            for (QAction *a : m->actions())
                if (a->text() == text)
                    return a;
            return nullptr;
        };
        QMenu *cells = find(menu, QStringLiteral("Cells"))->menu();
        QAction *four = nullptr;
        for (QAction *a : cells->actions())
            if (a->data().toInt() == 4)
                four = a;
        QVERIFY(four);
        four->trigger();
        QCOMPARE(grid().cells, 4);
        find(menu, QStringLiteral("Show horizon"))->toggle();
        QVERIFY(!grid().horizon);
        drag(at(Handle::RingY), QPointF(30, 0));
        QVERIFY(!(grid().orientation == orientation));
        find(menu, QStringLiteral("Reset rotation"))->trigger();
        QCOMPARE(grid().orientation, perspective::Grid().orientation);
        delete menu;

        // Locked: shown, but no handle takes the pointer.
        doc::LayerProperties locked = drawing.layer(id)->properties;
        locked.locked = true;
        controller.execute(doc::makeSetLayerProperties(d, controller.currentCel(), drawing.id, id, locked));
        const auto middle = gizmo.handlePoint(Handle::Move, canvas.view());
        QVERIFY(middle);
        QCOMPARE(gizmo.handleAt(*middle, canvas.view()), Handle::None);
        const perspective::Vec3 position = grid().position;
        drag(at(Handle::Move), QPointF(50, 0));
        QCOMPARE(grid().position, position);

        // Another layer current: no handles.
        controller.setCurrentLayer(drawing.layers.front().id);
        QVERIFY(!gizmo.target());
        QCOMPARE(gizmo.handleAt(*middle, canvas.view()), Handle::None);
    }


    // Smudge drags the color under the pen along; Blur softens an edge; each is one undo step, and a
    // drag that is cancelled leaves the layer as it was.
    void blendToolSmudgesAndBlurs()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage image(200, 100, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("red")); // x 860..1059, y 490..589
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
        canvas::BlendTool tool(&controller, &state, [](const QString &) {});
        state.setSize(tools::ToolId::Blend, 60);
        state.setOpacity(tools::ToolId::Blend, 1.0);

        QCOMPARE(qAlpha(pixel(1075, 540)), 0);
        tool.press({1000, 540}, Qt::NoModifier);
        QVERIFY(tool.isActive());
        for (int x = 1000; x <= 1120; x += 10)
            tool.move({double(x), 540}, Qt::NoModifier);
        tool.release({1120, 540}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QVERIFY(qAlpha(pixel(1075, 540)) > 60); // the red was pulled out past the edge
        QVERIFY(qRed(pixel(1075, 540)) > 0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo blend"));
        controller.undo();
        QCOMPARE(qAlpha(pixel(1075, 540)), 0);

        // Blur: the hard edge at x = 1059 softens on both sides.
        tools::BlendSettings blend;
        blend.mode = tools::BlendSettings::Mode::Blur;
        state.setBlendSettings(blend);
        QCOMPARE(qAlpha(pixel(1058, 540)), 255);
        QCOMPARE(qAlpha(pixel(1060, 540)), 0);
        tool.press({1059, 520}, Qt::NoModifier);
        for (int y = 520; y <= 560; y += 5)
            tool.move({1059, double(y)}, Qt::NoModifier);
        tool.release({1059, 560}, Qt::NoModifier);
        QVERIFY(qAlpha(pixel(1058, 540)) < 255);
        QVERIFY(qAlpha(pixel(1060, 540)) > 0);
        controller.undo();

        // Cancelled: nothing changes and nothing is left to undo.
        const bool couldUndo = controller.canUndo();
        tool.press({1000, 540}, Qt::NoModifier);
        tool.move({1100, 540}, Qt::NoModifier);
        tool.cancel();
        QVERIFY(!tool.isActive());
        QCOMPARE(qAlpha(pixel(1075, 540)), 0);
        QCOMPARE(controller.canUndo(), couldUndo);
    }


    // Liquify pushes the pixels along with the pen, and expands what is under it; the selection protects the rest.
    void liquifyToolMovesPixels()
    {
        tools::ToolState state;
        document::DocumentController controller;
        QImage bar(100, 10, QImage::Format_ARGB32);
        bar.fill(Qt::green);
        controller.importImage(bar, QStringLiteral("bar")); // x 910..1009, y 535..544
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto pixel = [&](int x, int y) { return cel.drawingAt(0)->layers.front().image.pixel(x, y); };
        canvas::LiquifyTool tool(&controller, &state, [](const QString &) {});
        state.setSize(tools::ToolId::Liquify, 80);
        state.setOpacity(tools::ToolId::Liquify, 1.0);

        QCOMPARE(qAlpha(pixel(915, 540)), 255);
        tool.press({905, 540}, Qt::NoModifier);
        for (int x = 905; x <= 960; x += 5)
            tool.move({double(x), 540}, Qt::NoModifier);
        tool.release({960, 540}, Qt::NoModifier);
        QVERIFY(qAlpha(pixel(915, 540)) < 128); // the left end of the bar was swept to the right
        QCOMPARE(controller.undoText(), QStringLiteral("Undo liquify"));
        controller.undo();
        QCOMPARE(qAlpha(pixel(915, 540)), 255);

        // Held by a selection, what lies outside does not move.
        QPainterPath area;
        area.addRect(900, 500, 30, 80); // x 900..929
        controller.setSelection(area);
        tool.press({905, 540}, Qt::NoModifier);
        for (int x = 905; x <= 960; x += 5)
            tool.move({double(x), 540}, Qt::NoModifier);
        tool.release({960, 540}, Qt::NoModifier);
        QCOMPARE(qAlpha(pixel(950, 540)), 255);
        controller.clearSelection();

        // Expand: a small square swells under the pen (in a document of its own).
        document::DocumentController other;
        QImage dot(20, 20, QImage::Format_ARGB32);
        dot.fill(Qt::red);
        other.importImage(dot, QStringLiteral("dot")); // x 950..969, y 530..549
        other.clearSelection();
        const doc::Cel &otherCel = *other.document().cel(other.currentCel());
        auto dotPixel = [&](int x, int y) { return otherCel.drawingAt(0)->layers.front().image.pixel(x, y); };
        canvas::LiquifyTool expand(&other, &state, [](const QString &) {});
        tools::LiquifySettings liquify;
        liquify.mode = tools::LiquifySettings::Mode::Expand;
        state.setLiquifySettings(liquify);
        QCOMPARE(qAlpha(dotPixel(972, 540)), 0);
        expand.press({960, 540}, Qt::NoModifier);
        for (int i = 0; i < 6; ++i) {
            expand.move({980, 540}, Qt::NoModifier);
            expand.move({960, 540}, Qt::NoModifier);
        }
        expand.release({960, 540}, Qt::NoModifier);
        QVERIFY(qAlpha(dotPixel(972, 540)) > 0);
    }


    // The blend and liquify tools have their sub tools, and picking one sets the mode.
    void blendAndLiquifyHaveSubTools()
    {
        tools::ToolState state;
        const auto blend = tools::builtInPresets(tools::ToolId::Blend);
        QCOMPARE(blend.size(), 2);
        tools::applyPreset(state, blend[1]);
        QCOMPARE(state.blendSettings().mode, tools::BlendSettings::Mode::Blur);
        const auto liquify = tools::builtInPresets(tools::ToolId::Liquify);
        QCOMPARE(liquify.size(), 3);
        tools::applyPreset(state, liquify[2]);
        QCOMPARE(state.liquifySettings().mode, tools::LiquifySettings::Mode::Shrink);
        QCOMPARE(state.settings(tools::ToolId::Blend).opacity, 0.6); // a mode keeps size and strength
    }


    // The shape tool draws a rectangle, an ellipse or a line as one undo step; the selection holds it,
    // a click draws nothing.
    void shapeToolDrawsShapes()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto alphaAt = [&](int x, int y) {
            return cel.drawingAt(0) ? qAlpha(cel.drawingAt(0)->layers.front().image.pixel(x, y)) : 0;
        };
        canvas::ShapeTool tool(&controller, &state, [](const QString &) {});
        state.setColor(Qt::red);
        state.setSize(tools::ToolId::Shape, 4);

        // A click only.
        tool.press({100, 100}, Qt::NoModifier);
        tool.release({100, 100}, Qt::NoModifier);
        QVERIFY(!controller.canUndo());

        // An outlined rectangle: the edge is drawn, the inside is not.
        tool.press({100, 100}, Qt::NoModifier);
        QVERIFY(tool.isActive());
        tool.move({200, 160}, Qt::NoModifier);
        tool.release({200, 160}, Qt::NoModifier);
        QVERIFY(!tool.isActive());
        QCOMPARE(alphaAt(150, 100), 255);
        QCOMPARE(alphaAt(100, 130), 255);
        QCOMPARE(alphaAt(150, 130), 0);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo shape"));
        controller.undo();
        QCOMPARE(alphaAt(150, 100), 0);

        // Filled, at half opacity, inside a selection that holds back the right part.
        tools::ShapeSettings shape = state.shapeSettings();
        shape.style = tools::ShapeSettings::Style::Filled;
        state.setShapeSettings(shape);
        state.setOpacity(tools::ToolId::Shape, 0.5);
        QPainterPath area;
        area.addRect(0, 0, 150, 1080);
        controller.setSelection(area);
        tool.press({100, 100}, Qt::NoModifier);
        tool.release({200, 160}, Qt::NoModifier);
        QVERIFY(qAbs(alphaAt(120, 130) - 128) <= 1);
        QCOMPARE(alphaAt(180, 130), 0);
        controller.undo();
        controller.clearSelection();

        // Shift makes a circle, Alt grows it from the center.
        shape.kind = tools::ShapeSettings::Kind::Ellipse;
        state.setShapeSettings(shape);
        state.setOpacity(tools::ToolId::Shape, 1.0);
        tool.press({300, 300}, Qt::AltModifier | Qt::ShiftModifier);
        tool.release({340, 310}, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(alphaAt(300, 300), 255); // the center
        QCOMPARE(alphaAt(300, 262), 255); // 40 up: a circle, not a 10 px high ellipse
        QCOMPARE(alphaAt(300, 350), 0);   // beyond the radius
    }


    // A polygon is made of clicks: the first point closes it, Enter or a double click finish it, Esc drops it.
    void shapeToolPolygonAndCurveTakeClicks()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        auto alphaAt = [&](int x, int y) {
            return cel.drawingAt(0) ? qAlpha(cel.drawingAt(0)->layers.front().image.pixel(x, y)) : 0;
        };
        canvas::ShapeTool tool(&controller, &state, [](const QString &) {});
        state.setSize(tools::ToolId::Shape, 4);
        tools::ShapeSettings shape;
        shape.kind = tools::ShapeSettings::Kind::Polygon;
        shape.style = tools::ShapeSettings::Style::Filled;
        state.setShapeSettings(shape);

        auto click = [&](double x, double y) {
            tool.press({x, y}, Qt::NoModifier);
            QVERIFY(!tool.isActive()); // no drag: the canvas does not hold the tool
            tool.release({x, y}, Qt::NoModifier);
        };
        click(100, 100);
        QVERIFY(tool.hasPendingEdit());
        click(200, 100);
        click(200, 200);
        QVERIFY(!controller.canUndo()); // nothing drawn yet
        click(100, 100);                // back on the first point: done
        QVERIFY(!tool.hasPendingEdit());
        QCOMPARE(alphaAt(180, 110), 255); // inside the triangle
        QCOMPARE(alphaAt(110, 190), 0);   // the other half of the square
        QCOMPARE(controller.undoText(), QStringLiteral("Undo shape"));
        controller.undo();

        // Esc drops it; two points are too few for a polygon.
        click(100, 100);
        click(200, 100);
        QVERIFY(tool.keyPress(Qt::Key_Escape));
        QVERIFY(!tool.hasPendingEdit());
        click(100, 100);
        click(200, 100);
        tool.commit();
        QVERIFY(!controller.canUndo());

        // A curve goes through its points and is finished by Enter; Backspace takes a point back.
        shape.kind = tools::ShapeSettings::Kind::Curve;
        state.setShapeSettings(shape);
        click(100, 300);
        click(150, 250);
        click(200, 300);
        click(250, 350);
        QVERIFY(tool.keyPress(Qt::Key_Backspace));
        QVERIFY(tool.keyPress(Qt::Key_Return));
        QCOMPARE(alphaAt(150, 250), 255);
        QCOMPARE(alphaAt(200, 300), 255);
        QCOMPARE(alphaAt(250, 350), 0); // the removed point
        QVERIFY(!tool.keyPress(Qt::Key_Return)); // nothing in progress
    }


    void shapeToolRoundedRectangleRoundsItsCorners()
    {
        tools::ToolState state;
        document::DocumentController controller;
        controller.clearSelection();
        const doc::Cel &cel = *controller.document().cel(controller.currentCel());
        canvas::ShapeTool tool(&controller, &state, [](const QString &) {});
        tools::ShapeSettings shape;
        shape.kind = tools::ShapeSettings::Kind::RoundedRectangle;
        shape.style = tools::ShapeSettings::Style::Filled;
        shape.cornerRadius = 30;
        state.setShapeSettings(shape);
        tool.press({100, 100}, Qt::NoModifier);
        tool.release({300, 200}, Qt::NoModifier);
        const auto &image = cel.drawingAt(0)->layers.front().image;
        QCOMPARE(qAlpha(image.pixel(200, 150)), 255);
        QCOMPARE(qAlpha(image.pixel(101, 101)), 0); // the corner is cut off
        QCOMPARE(qAlpha(image.pixel(200, 101)), 255);
    }


    void shapeGeometryConstrains()
    {
        using Kind = tools::ShapeSettings::Kind;
        const QPointF a(10, 10);
        const auto square = canvas::shapeGeometry(Kind::Rectangle, a, {50, 30}, Qt::ShiftModifier);
        QCOMPARE(square.box, QRectF(10, 10, 40, 40));
        const auto centered = canvas::shapeGeometry(Kind::Rectangle, a, {20, 15}, Qt::AltModifier);
        QCOMPARE(centered.box, QRectF(0, 5, 20, 10));
        const auto flat = canvas::shapeGeometry(Kind::Line, a, {50, 14}, Qt::ShiftModifier); // ~5.7°: snaps to 0°
        QVERIFY(qAbs(flat.line.p2().y() - 10) < 1e-6);
        QVERIFY(qAbs(flat.line.length() - QLineF(a, QPointF(50, 14)).length()) < 1e-6);
    }


    void shapeHasSubTools()
    {
        tools::ToolState state;
        const auto presets = tools::builtInPresets(tools::ToolId::Shape);
        QCOMPARE(presets.size(), 6);
        tools::applyPreset(state, presets[3]);
        QCOMPARE(state.shapeSettings().kind, tools::ShapeSettings::Kind::Ellipse);
        tools::applyPreset(state, presets[5]);
        QCOMPARE(state.shapeSettings().kind, tools::ShapeSettings::Kind::Curve);
        QCOMPARE(state.settings(tools::ToolId::Shape).size, 4.0); // a kind keeps the thickness
    }


    // The snap picks the axis closest to the direction of the pen, once it has moved far enough, and
    // puts the pen on that axis's line through the start.
    void perspectiveSnapChoosesTheNearestAxis()
    {
        const perspective::Grid grid = perspective::defaultGrid(1920, 1080);
        const QPointF start(900, 500);
        const auto dirs = canvas::PerspectiveSnap::directions(grid, start);
        for (int axis = 0; axis < 3; ++axis) {
            if (!dirs[std::size_t(axis)])
                continue;
            const QPointF u = *dirs[std::size_t(axis)];
            const double a = 5 * std::numbers::pi / 180; // a little off the axis
            const QPointF off(u.x() * std::cos(a) - u.y() * std::sin(a), u.x() * std::sin(a) + u.y() * std::cos(a));
            canvas::PerspectiveSnap snap;
            snap.begin(grid, start, 10.0);
            QVERIFY(!snap.update(start + off * 5)); // not far enough: not decided yet
            QVERIFY(!snap.decided());
            QVERIFY(snap.update(start + off * 100));
            // (two axes can share a direction: the one found must be as close as this)
            const QPointF onLine = snap.snapped(start + off * 100);
            const QPointF along = *dirs[std::size_t(snap.axis())];
            const QPointF w = onLine - start;
            QVERIFY(std::abs(w.x() * along.y() - w.y() * along.x()) < 1e-6); // on the line through the start
            QVERIFY(std::hypot(w.x(), w.y()) > 90 && std::hypot(w.x(), w.y()) < 101);
            // Kept: turning the pen another way afterwards does not change the axis.
            const int chosen = snap.axis();
            snap.update(start + QPointF(-off.y(), off.x()) * 100);
            QCOMPARE(snap.axis(), chosen);
        }
    }


    // Shift held: a straight line from where the last stroke ended to the pen, drawn when the pen touches;
    // letting go of Shift drops it; until a touch nothing is drawn.
    void heldShiftPlacesAStraightLineFromTheLastPoint()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setColor(Qt::red);
        state.setSize(tools::ToolId::Brush, 10.0);
        auto alphaAt = [&](const QPointF &docPoint) {
            for (const doc::Layer &l : controller.currentDrawing()->layers)
                if (l.id == controller.currentLayer())
                    return qAlpha(l.image.pixel(int(docPoint.x()), int(docPoint.y())));
            return -1;
        };
        const QTransform toView = canvas.view().documentToView();
        auto view = [&](const QPointF &p) { return toView.map(p).toPoint(); };
        auto tap = [&](const QPointF &p) {
            QTest::mousePress(&canvas, Qt::LeftButton, {}, view(p));
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, view(p));
        };
        const QPointF last(100, 100), target(300, 160);

        // Nothing drawn yet: Shift has no point to start from, the touch is an ordinary dot.
        QTest::keyPress(&canvas, Qt::Key_Shift);
        QTest::mouseMove(&canvas, view(target));
        tap(last);
        QTest::keyRelease(&canvas, Qt::Key_Shift);
        QVERIFY(alphaAt(last) > 0);
        QCOMPARE(alphaAt((last + target) / 2.0), 0);

        // Held: the line shows (draws nothing) until the touch places it, one undo step.
        QTest::keyPress(&canvas, Qt::Key_Shift);
        QTest::mouseMove(&canvas, view(target));
        QCOMPARE(alphaAt((last + target) / 2.0), 0);
        tap(target);
        QVERIFY(alphaAt((last + target) / 2.0) > 0);
        QVERIFY(alphaAt(QPointF(target.x() - 2, target.y() - 0.7)) > 0);
        QCOMPARE(alphaAt(QPointF(200, 100)), 0); // only the line

        // Still held: the next line starts where this one ended.
        const QPointF third(300, 300);
        tap(third);
        QVERIFY(alphaAt(QPointF(300, 230)) > 0);
        QTest::keyRelease(&canvas, Qt::Key_Shift);
        controller.undo();
        controller.undo(); // the two lines
        QCOMPARE(alphaAt((last + target) / 2.0), 0);
        QVERIFY(alphaAt(last) > 0);

        // Let go of Shift: the touch is an ordinary dot again.
        QTest::keyPress(&canvas, Qt::Key_Shift);
        QTest::keyRelease(&canvas, Qt::Key_Shift);
        tap(QPointF(400, 400));
        QCOMPARE(alphaAt(QPointF(250, 250)), 0);
        QVERIFY(alphaAt(QPointF(400, 400)) > 0);
    }


    // Through a fisheye the axis's line is a curve: the pen is put on it, and it starts where the stroke did.
    void perspectiveSnapFollowsTheCurveOfAFisheye()
    {
        perspective::Grid grid = perspective::defaultGrid(800, 600);
        grid.fisheye = 0.8;
        const QPointF start(300, 250);
        const auto dirs = canvas::PerspectiveSnap::directions(grid, start);
        QVERIFY(dirs[0] && dirs[1] && dirs[2]);
        canvas::PerspectiveSnap snap;
        snap.begin(grid, start, 6.0);
        QVERIFY(snap.update(start + *dirs[0] * 40.0));
        QCOMPARE(snap.axis(), 0);
        const QPointF origin = snap.snapped(start);
        QVERIFY(QLineF(origin, start).length() < 1e-6);
        // Whatever the pen does, the point stays in the plane of the start's ray and the axis.
        const perspective::Vec3 ray = perspective::unprojectDirection(grid, {start.x(), start.y()});
        const perspective::Vec3 along = perspective::axis(grid, {1, 0, 0});
        const perspective::Vec3 n = perspective::cross(ray, along);
        const double length = std::sqrt(perspective::dot(n, n));
        for (const QPointF pen : {QPointF(420, 330), QPointF(150, 100), QPointF(500, 200)}) {
            const QPointF on = snap.snapped(pen);
            const perspective::Vec3 d = perspective::unprojectDirection(grid, {on.x(), on.y()});
            QVERIFY(std::abs(perspective::dot(d, n)) / length < 1e-6);
        }
        QVERIFY(snap.guide(QPointF(420, 330)).size() > 2); // a curve, not two points
    }


    // A stroke made with the snap on follows an axis of the chosen perspective layer instead of the pen.
    void perspectiveSnapBendsTheStroke()
    {
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setColor(Qt::red);
        state.setSize(tools::ToolId::Brush, 12.0);
        auto stroke = [&](const QPointF &from, const QPointF &to) {
            const QTransform toView = canvas.view().documentToView();
            const QPoint a = toView.map(from).toPoint(), b = toView.map(to).toPoint();
            QTest::mousePress(&canvas, Qt::LeftButton, {}, a);
            for (int i = 1; i <= 8; ++i)
                QTest::mouseMove(&canvas, a + (b - a) * i / 8);
            QTest::mouseRelease(&canvas, Qt::LeftButton, {}, b);
        };
        stroke({100, 100}, {120, 100}); // makes the drawing, and its layer
        const doc::LayerId raster = controller.currentLayer();
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::LayerId gridLayer = controller.currentLayer();
        controller.setCurrentLayer(raster);
        QVERIFY(controller.perspectiveLayerById(gridLayer));
        const doc::Layer *drawn = nullptr;
        for (const doc::Layer &l : controller.currentDrawing()->layers)
            if (l.id == raster)
                drawn = &l;
        QVERIFY(drawn);
        auto alpha = [&](const QPointF &p) { return qAlpha(drawn->image.pixel(int(p.x()), int(p.y()))); };

        const perspective::Grid grid = controller.perspectiveLayerById(gridLayer)->grid;
        const QPointF start(900, 500);
        const auto dirs = canvas::PerspectiveSnap::directions(grid, start);
        QVERIFY(dirs[0]);
        const QPointF u = *dirs[0];
        const double a = 6 * std::numbers::pi / 180;
        const QPointF off(u.x() * std::cos(a) - u.y() * std::sin(a), u.x() * std::sin(a) + u.y() * std::cos(a));
        const QPointF end = start + off * 200;

        // Without snapping: along the pen.
        stroke(start, end);
        QVERIFY(alpha(start + off * 150) > 0);
        controller.undo();
        QCOMPARE(alpha(start + off * 150), 0);

        // With it: along the axis; where the pen went is left clear.
        state.setPerspectiveSnap(true);
        state.setPerspectiveSnapLayer(gridLayer);
        stroke(start, end);
        QVERIFY(alpha(start + u * 150) > 0);
        QCOMPARE(alpha(start + off * 150), 0); // 16 px off the axis
        QVERIFY(alpha(start + u * 5) > 0);     // and from the start
    }


    // Lines of the grid partly behind the eye are cut at the near limit; the cut point used to round to just
    // behind it and the whole line was lost (seen with a 120° lens).
    void perspectiveGridKeepsLinesCutByTheNearLimit()
    {
        for (double tilt : {45.0, 60.0, 75.0})
            for (double turn = 0; turn < 360; turn += 15) {
                perspective::Grid grid = perspective::defaultGrid(1920, 1080);
                grid.fov = 120.0;
                grid.orientation = perspective::fromAngles(turn, tilt, 0.0);
                const auto segments = perspective::gridSegments(grid);
                // A line is kept when any part of it is in front of the eye: count them the slow way.
                int expected = 0;
                const int n = grid.cells;
                const double f = perspective::focalLength(grid), extent = n * grid.cell;
                auto in_front = [&](double x0, double y0, double x1, double y1) {
                    for (int s = 0; s <= 2000; ++s) {
                        const double t = s / 2000.0;
                        if (perspective::project(grid, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t))
                            return true;
                    }
                    return false;
                };
                for (int i = -n; i <= n; ++i) {
                    expected += in_front(i * grid.cell, -extent, i * grid.cell, extent);
                    expected += in_front(-extent, i * grid.cell, extent, i * grid.cell);
                }
                (void)f;
                QCOMPARE(int(segments.size()), expected);
            }
    }


    // The next axis, in order, and the stroke in progress drawn again on it: the A key. The guide (an outline
    // and a letter) is in the color of the axis the stroke is on.
    void perspectiveSnapAxisCanBeCycledMidStroke()
    {
        const perspective::Grid grid = perspective::defaultGrid(1920, 1080);
        canvas::PerspectiveSnap snap;
        snap.begin(grid, QPointF(900, 500), 10.0);
        QCOMPARE(snap.cycle(), 0); // undecided: the first
        QCOMPARE(snap.axis(), 0);
        QCOMPARE(snap.cycle(), 1);
        QCOMPARE(snap.cycle(), 2);
        QCOMPARE(snap.cycle(), 0);
        QVERIFY(canvas::PerspectiveSnap::axisColor(0) != canvas::PerspectiveSnap::axisColor(1));
        QCOMPARE(canvas::PerspectiveSnap::axisLetter(2), QChar('Z'));

        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.resize(900, 700);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setColor(Qt::black);
        state.setSize(tools::ToolId::Brush, 12.0);
        auto toView = [&](const QPointF &p) { return canvas.view().documentToView().map(p).toPoint(); };
        auto drag = [&](const QPointF &from, const QPointF &to, int steps) {
            const QPoint a = toView(from), b = toView(to);
            for (int i = 1; i <= steps; ++i)
                QTest::mouseMove(&canvas, a + (b - a) * i / steps);
        };
        QTest::mousePress(&canvas, Qt::LeftButton, {}, toView({100, 100}));
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, toView({120, 100}));
        const doc::LayerId raster = controller.currentLayer();
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::LayerId gridLayer = controller.currentLayer();
        controller.setCurrentLayer(raster);
        const perspective::Grid g = controller.perspectiveLayerById(gridLayer)->grid;
        state.setPerspectiveSnap(true);
        state.setPerspectiveSnapLayer(gridLayer);
        const doc::Layer *drawn = nullptr;
        for (const doc::Layer &l : controller.currentDrawing()->layers)
            if (l.id == raster)
                drawn = &l;
        QVERIFY(drawn);
        auto alpha = [&](const QPointF &p) { return qAlpha(drawn->image.pixel(int(p.x()), int(p.y()))); };
        // Pixels of the guide in the color of an axis (the grabbed canvas, the sign of the color is enough).
        auto guidePixels = [&](int axis) {
            const QImage shot = canvas.grab().toImage();
            const QColor c = canvas::PerspectiveSnap::axisColor(axis);
            int n = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QColor p = shot.pixelColor(x, y);
                    n += std::abs(p.red() - c.red()) + std::abs(p.green() - c.green()) + std::abs(p.blue() - c.blue()) < 40;
                }
            return n;
        };

        const QPointF start(900, 500);
        const auto dirs = canvas::PerspectiveSnap::directions(g, start);
        QVERIFY(dirs[0] && dirs[1]);
        const QPointF u0 = *dirs[0], u1 = *dirs[1];
        QVERIFY(std::abs(u0.x() * u1.y() - u0.y() * u1.x()) > 0.3); // the axes are well apart here
        const double a = 4 * std::numbers::pi / 180;
        const QPointF off(u0.x() * std::cos(a) - u0.y() * std::sin(a), u0.x() * std::sin(a) + u0.y() * std::cos(a));

        QTest::mousePress(&canvas, Qt::LeftButton, {}, toView(start));
        drag(start, start + off * 120, 8);
        QVERIFY(alpha(start + u0 * 60) > 0); // on the first axis
        QVERIFY(guidePixels(0) > 20);        // in its color
        QCOMPARE(guidePixels(1), 0);

        QTest::keyClick(&canvas, Qt::Key_A); // the next axis
        QCOMPARE(alpha(start + u0 * 60), 0);  // what was drawn on the first is gone
        QVERIFY(guidePixels(1) > 20);
        QCOMPARE(guidePixels(0), 0);
        drag(start + off * 120, start + off * 200, 4);
        QTest::mouseRelease(&canvas, Qt::LeftButton, {}, toView(start + off * 200));
        // The stroke ends along the second axis, from the start (either way along it).
        bool inked = false;
        for (double t = -200; t <= 200; t += 5)
            inked = inked || alpha(start + u1 * t) > 0;
        QVERIFY(inked);
        QCOMPARE(alpha(start + u0 * 60), 0);
        QCOMPARE(guidePixels(1), 0); // the guide is gone with the stroke
    }


    // A perspective layer is found by its id, in any frame.
    void perspectiveLayersAreFoundById()
    {
        document::DocumentController controller;
        QImage image(10, 10, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller.importImage(image, QStringLiteral("x"));
        const doc::LayerId raster = controller.currentLayer();
        QVERIFY(!controller.perspectiveLayerById(doc::kNoId));
        QVERIFY(!controller.perspectiveLayerById(raster)); // a layer of pixels is not one
        QVERIFY(controller.addLayer(true, doc::LayerKind::Perspective));
        const doc::LayerId grid = controller.currentLayer();
        const auto found = controller.perspectiveLayerById(grid);
        QVERIFY(found);
        QCOMPARE(found->ref.layer, grid);
        controller.setCurrentFrame(5); // wherever the timeline is
        QVERIFY(controller.perspectiveLayerById(grid));
        QVERIFY(!controller.perspectiveLayerById(99999));
    }


    // A polygon in progress has a bar with OK and Cancel under its points; OK waits for enough points.
    void shapeBarFinishesOrDrops()
    {
        ui::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *controller = window.findChild<document::DocumentController *>();
        auto *state = window.findChild<tools::ToolState *>();
        auto *canvas = window.findChild<canvas::CanvasView *>();
        auto *bar = window.findChild<ui::ShapeBar *>();
        QVERIFY(bar);
        controller->clearSelection();
        state->setActiveTool(tools::ToolId::Shape);
        tools::ShapeSettings shape;
        shape.kind = tools::ShapeSettings::Kind::Polygon;
        shape.style = tools::ShapeSettings::Style::Filled;
        state->setShapeSettings(shape);
        QVERIFY(!bar->isVisible());

        auto at = [&](QPointF doc) { return canvas->view().documentToView().map(doc).toPoint(); };
        auto click = [&](QPointF doc) {
            QTest::mouseClick(canvas, Qt::LeftButton, {}, at(doc));
            QCoreApplication::processEvents();
        };
        auto *ok = bar->findChild<QToolButton *>(QStringLiteral("ok"));
        auto *cancel = bar->findChild<QToolButton *>(QStringLiteral("cancel"));
        QVERIFY(ok && cancel);
        click({900, 500});
        QVERIFY(bar->isVisible());
        QVERIFY(!ok->isEnabled()); // one point
        click({1000, 500});
        click({1000, 600});
        QVERIFY(ok->isEnabled());
        const QRect points = canvas->view().documentToView().mapRect(canvas->shapeBounds()).toAlignedRect();
        QVERIFY(bar->geometry().top() >= points.bottom() || bar->geometry().bottom() <= points.top());

        QTest::mouseClick(cancel, Qt::LeftButton);
        QVERIFY(!bar->isVisible());
        QVERIFY(!controller->canUndo());

        click({900, 500});
        click({1000, 500});
        click({1000, 600});
        QTest::mouseClick(ok, Qt::LeftButton);
        QVERIFY(!bar->isVisible());
        QCOMPARE(controller->undoText(), QStringLiteral("Undo shape"));
        const doc::Cel &cel = *controller->document().cel(controller->currentCel());
        QCOMPARE(qAlpha(cel.drawingAt(0)->layers.front().image.pixel(980, 510)), 255);
    }


    // With a drawing tool and the outline on, the canvas shows no system pointer (the brush's outline is the
    // pointer); with the outline off, or another tool, it does.
    void paintToolsHideTheSystemPointer()
    {
        tools::Preferences &prefs = tools::Preferences::instance();
        prefs.resetAll();
        tools::ToolState state;
        document::DocumentController controller;
        canvas::CanvasView canvas(&state, &controller);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        state.setActiveTool(tools::ToolId::Brush);
        QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
        QVERIFY(canvas.cursor().pixmap().hasAlphaChannel());
        QCOMPARE(qAlpha(canvas.cursor().pixmap().toImage().pixel(4, 4)), 0); // transparent
        state.setActiveTool(tools::ToolId::Eraser);
        QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
        state.setActiveTool(tools::ToolId::Fill);
        QVERIFY(canvas.cursor().shape() != Qt::BitmapCursor || !canvas.cursor().pixmap().isNull());
        // The zoom tool has a magnifying glass of its own (not the arrows of a horizontal resize).
        state.setActiveTool(tools::ToolId::Zoom);
        QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
        QVERIFY(!canvas.cursor().pixmap().isNull());
        QCOMPARE(canvas.cursor().hotSpot(), QPoint(13, 13));
        // Something else changed the cursor of the window (a dock separator, a panel): the pen or mouse seen on the
        // canvas puts the canvas' own back, without waiting for the next enter.
        QWindow *handle = canvas.window()->windowHandle();
        QVERIFY(handle);
        state.setActiveTool(tools::ToolId::Brush);
        handle->setCursor(Qt::SplitHCursor);
        QTest::mouseMove(&canvas, QPoint(50, 50));
        QCOMPARE(handle->cursor().shape(), Qt::BitmapCursor);
        handle->setCursor(Qt::ArrowCursor);
        QTest::mouseMove(&canvas, QPoint(60, 60));
        QCOMPARE(handle->cursor().shape(), Qt::BitmapCursor);
        prefs.setBrushOutline(false);
        state.setActiveTool(tools::ToolId::Brush);
        QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor); // the pointer is back
        prefs.setBrushOutline(true);
        QCOMPARE(canvas.cursor().shape(), Qt::BitmapCursor);
        prefs.resetAll();
    }

};

QTEST_MAIN(TestUiDrawing)
#include "tst_ui_drawing.moc"
