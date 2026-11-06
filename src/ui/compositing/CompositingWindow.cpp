#include "ui/compositing/CompositingWindow.h"

#include "document/DocumentController.h"
#include "io/FrameExport.h"
#include "ui/compositing/ImagePreview.h"
#include "ui/compositing/NodeGraphView.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QLabel>
#include <QToolButton>
#include <QPainter>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace mothion::ui {

using compositing::MaskShape;
using compositing::parseMaskShapes;
using compositing::maskShapesToJson;

CompositingWindow::CompositingWindow(document::DocumentController *document, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_document(document)
    , m_graph(nullptr)
    , m_preview(new ImagePreview(this))
    , m_timer(new QTimer(this))
    , m_viewTimer(new QTimer(this))
    , m_frameTimer(new QTimer(this))
    , m_commitTimer(new QTimer(this))
{
    // The project's network; a project that has none yet starts with a small one to look at.
    if (m_document && !m_document->compositing().nodes.empty())
        m_network.load(m_document->compositing());
    else
        seed();
    if (m_document)
        m_committed = m_document->compositing();
    m_graph = new NodeGraphView(m_network, this);

    setWindowTitle(tr("Compositing"));
    resize(1100, 700); // what restoring from maximized gives

    // Buttons for what the wheel, the middle button and Shift do: a tablet pen has none of them.
    auto makeButton = [](QWidget *owner, const QString &text, const QString &tip, bool checkable = false) {
        auto *button = new QToolButton(owner);
        button->setText(text);
        button->setToolTip(tip);
        button->setCheckable(checkable);
        button->setMinimumSize(36, 30);
        return button;
    };

    auto *lower = new QWidget(this);
    auto *lowerLayout = new QVBoxLayout(lower);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->setSpacing(0);
    auto *graphBar = new QWidget(lower);
    auto *graphBarLayout = new QHBoxLayout(graphBar);
    graphBarLayout->setContentsMargins(8, 4, 8, 4);
    auto *graphOut = makeButton(graphBar, QStringLiteral("−"), tr("Zoom out the nodes"));
    auto *graphIn = makeButton(graphBar, QStringLiteral("+"), tr("Zoom in the nodes"));
    auto *graphFit = makeButton(graphBar, tr("Fit"), tr("Show all the nodes"));
    auto *graphSelect = makeButton(graphBar, tr("Select"), tr("Drag on the empty ground to select nodes"), true);
    auto *graphPan = makeButton(graphBar, tr("Pan"), tr("Drag to move the view of the nodes"), true);
    auto *graphCut = makeButton(graphBar, tr("Cut"), tr("Drag across wires to cut them"), true);
    graphSelect->setChecked(true);
    auto *modes = new QButtonGroup(graphBar);
    for (QToolButton *b : {graphSelect, graphPan, graphCut})
        modes->addButton(b);
    for (QToolButton *b : {graphOut, graphIn, graphFit})
        graphBarLayout->addWidget(b);
    graphBarLayout->addSpacing(12);
    for (QToolButton *b : {graphSelect, graphPan, graphCut})
        graphBarLayout->addWidget(b);
    graphBarLayout->addStretch(1);
    connect(graphOut, &QToolButton::clicked, m_graph, [this] { m_graph->zoomBy(1 / 1.25); });
    connect(graphIn, &QToolButton::clicked, m_graph, [this] { m_graph->zoomBy(1.25); });
    connect(graphFit, &QToolButton::clicked, m_graph, [this] { m_graph->fitAll(); });
    connect(graphSelect, &QToolButton::clicked, m_graph, [this] { m_graph->setMode(NodeGraphView::Mode::Select); });
    connect(graphPan, &QToolButton::clicked, m_graph, [this] { m_graph->setMode(NodeGraphView::Mode::Pan); });
    connect(graphCut, &QToolButton::clicked, m_graph, [this] { m_graph->setMode(NodeGraphView::Mode::Cut); });
    lowerLayout->addWidget(graphBar);
    lowerLayout->addWidget(m_graph, 1);

    // The picture, with the tools of the Mask node being edited above it (shown only then).
    m_maskBar = new QWidget(this);
    auto *barLayout = new QHBoxLayout(m_maskBar);
    barLayout->setContentsMargins(8, 4, 8, 4);
    auto *group = new QButtonGroup(this);
    auto addTool = [&](const QString &text, const QString &tip, ImagePreview::Tool tool, bool checked) {
        auto *button = new QToolButton(m_maskBar);
        button->setText(text);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setChecked(checked);
        group->addButton(button);
        barLayout->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, tool] { m_preview->setTool(tool); });
    };
    addTool(tr("Rectangle"), tr("Drag to add a rectangle to the mask"), ImagePreview::Tool::Rectangle, true);
    addTool(tr("Lasso"), tr("Draw a free shape around an area to add it to the mask"), ImagePreview::Tool::Lasso, false);
    addTool(tr("Move"), tr("Drag to move the picture (the middle button always does)"), ImagePreview::Tool::Pan, false);
    barLayout->addSpacing(12);
    auto *removeLast = new QToolButton(m_maskBar);
    removeLast->setText(tr("Remove last"));
    removeLast->setToolTip(tr("Take out the area drawn last"));
    barLayout->addWidget(removeLast);
    auto *clear = new QToolButton(m_maskBar);
    clear->setText(tr("Clear"));
    clear->setToolTip(tr("Take out all the areas"));
    barLayout->addWidget(clear);
    barLayout->addStretch(1);
    barLayout->addWidget(new QLabel(tr("Mask: the areas drawn are in red"), m_maskBar));
    m_maskBar->hide();
    connect(removeLast, &QToolButton::clicked, this, [this] {
        QList<MaskShape> shapes = maskShapes();
        if (!shapes.isEmpty()) {
            shapes.removeLast();
            setMaskShapes(shapes);
        }
    });
    connect(clear, &QToolButton::clicked, this, [this] { setMaskShapes({}); });
    auto *upper = new QWidget(this);
    auto *upperLayout = new QVBoxLayout(upper);
    upperLayout->setContentsMargins(0, 0, 0, 0);
    upperLayout->setSpacing(0);
    upperLayout->addWidget(m_maskBar);
    auto *viewBar = new QWidget(upper);
    auto *viewBarLayout = new QHBoxLayout(viewBar);
    viewBarLayout->setContentsMargins(8, 4, 8, 4);
    auto *viewOut = makeButton(viewBar, QStringLiteral("−"), tr("Zoom out the picture"));
    auto *viewIn = makeButton(viewBar, QStringLiteral("+"), tr("Zoom in the picture"));
    auto *viewFit = makeButton(viewBar, tr("Fit"), tr("Fit the picture in the view"));
    auto *viewActual = makeButton(viewBar, QStringLiteral("100%"), tr("One pixel of the picture for one on the screen"));
    for (QToolButton *b : {viewOut, viewIn, viewFit, viewActual})
        viewBarLayout->addWidget(b);
    viewBarLayout->addStretch(1);
    connect(viewOut, &QToolButton::clicked, m_preview, [this] { m_preview->zoomBy(1 / 1.25); });
    connect(viewIn, &QToolButton::clicked, m_preview, [this] { m_preview->zoomBy(1.25); });
    connect(viewFit, &QToolButton::clicked, m_preview, &ImagePreview::fit);
    connect(viewActual, &QToolButton::clicked, m_preview, &ImagePreview::actualSize);
    upperLayout->addWidget(viewBar);
    upperLayout->addWidget(m_preview, 1);
    connect(m_preview, &ImagePreview::rectangleDrawn, this,
            [this](const QRectF &r) { addMaskShape(MaskShape::rectangle(r)); });
    connect(m_preview, &ImagePreview::lassoDrawn, this,
            [this](const QPolygonF &poly) { addMaskShape(MaskShape::lasso(poly)); });
    connect(m_graph, &NodeGraphView::selectedNodeChanged, this, &CompositingWindow::editMask);

    auto *split = new QSplitter(Qt::Vertical, this);
    m_split = split;
    split->setChildrenCollapsible(false);
    split->addWidget(upper);
    split->addWidget(lower);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split);

    // Changes come in bursts (a stroke, a drag): run the nodes once they pause.
    m_timer->setSingleShot(true);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, &CompositingWindow::refresh);
    // Every edit is kept in the document a moment after the last one, or when the mouse is released (a drag,
    // a slider, a wire is then done): a gesture is one undo step, not one for each pixel.
    m_commitTimer->setSingleShot(true);
    m_commitTimer->setInterval(250);
    connect(m_commitTimer, &QTimer::timeout, this, &CompositingWindow::commit);
    connect(m_graph, &NodeGraphView::gestureEnded, this, &CompositingWindow::commit);
    m_listener = m_network.addListener([this](const compositing::GraphChange &change) {
        if (change.kind != compositing::GraphChange::Kind::Moved)
            scheduleRefresh(); // moving a node changes no picture
        if (!m_loading && change.kind != compositing::GraphChange::Kind::Reset) {
            m_pending = true;
            m_commitTimer->start();
        }
    });
    if (m_document) {
        connect(m_document, &document::DocumentController::structureChanged, this, &CompositingWindow::documentChanged);
        // The picture the Frame nodes hold is made again only when the drawing or the frame changed: while
        // only the nodes change it stays the same image, which is how the evaluator knows what it can keep.
        auto frameChanged = [this] {
            m_frameDirty = true;
            scheduleRefresh();
        };
        connect(m_document, &document::DocumentController::currentFrameChanged, this, frameChanged);
        connect(m_document, &document::DocumentController::structureChanged, this, frameChanged);
        connect(m_document, &document::DocumentController::pixelsChanged, this, frameChanged);
    }
    // The window gets its size a moment after it is shown (maximized): frame the layout then, and again
    // if the size changes, until the user takes the view into their hands.
    m_frameTimer->setSingleShot(true);
    m_frameTimer->setInterval(60);
    connect(m_frameTimer, &QTimer::timeout, this, &CompositingWindow::frame);
    connect(m_graph, &NodeGraphView::viewTouched, this, [this] {
        m_autoFrame = false;
        m_viewTimer->start();
    });
    m_viewTimer->setSingleShot(true);
    m_viewTimer->setInterval(300);
    connect(m_viewTimer, &QTimer::timeout, this, &CompositingWindow::keepView);
    for (QScrollBar *bar : {m_graph->horizontalScrollBar(), m_graph->verticalScrollBar()})
        connect(bar, &QScrollBar::valueChanged, m_viewTimer, qOverload<>(&QTimer::start));
    if (m_document)
        connect(m_document, &document::DocumentController::documentReplaced, this, [this] {
            m_autoFrame = true; // another project: its view, or the first look
            m_frameTimer->start();
        });
    connect(split, &QSplitter::splitterMoved, this, [this] { m_autoFrame = false; });
    refresh();
}

void CompositingWindow::seed()
{
    // The frame straight to the output.
    const compositing::NodeId source = m_network.addNode(QStringLiteral("frame"), {-200, -30});
    const compositing::NodeId out = m_network.addNode(QStringLiteral("output"), {200, -30});
    m_network.connect(source, 0, out, 0);
}

void CompositingWindow::commit()
{
    m_commitTimer->stop();
    if (!m_pending || !m_document)
        return;
    m_pending = false;
    const doc::GraphData now = m_network.toData();
    if (now == m_committed)
        return;
    m_committed = now; // before: the document's change comes back through documentChanged()
    m_document->setCompositing(now);
}

void CompositingWindow::documentChanged()
{
    if (!m_document || m_document->compositing() == m_committed)
        return; // not the network, or the very step this window just made
    m_committed = m_document->compositing();
    m_pending = false;
    m_commitTimer->stop();
    m_loading = true;
    m_network.load(m_committed);
    if (m_network.nodes().empty()) // undone to before the first edit
        seed();
    m_loading = false;
    scheduleRefresh();
}

CompositingWindow::~CompositingWindow()
{
    commit();
    keepView();
    // The editor listens to the network, which goes first among the members: it cannot wait for the children.
    m_network.removeListener(m_listener);
    delete m_graph;
}

void CompositingWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_autoFrame)
        m_frameTimer->start();
}

void CompositingWindow::frame()
{
    if (!m_autoFrame)
        return;
    // Half and half, the nodes where the project left them (or at their own size).
    const int half = m_split->height() / 2;
    m_split->setSizes({half, m_split->height() - half});
    applyView();
}

void CompositingWindow::applyView()
{
    const doc::GraphView saved = m_document ? m_document->compositingView() : doc::GraphView();
    if (saved.valid)
        m_graph->setView(saved.zoom, {saved.x, saved.y});
    else
        m_graph->showStart();
}

void CompositingWindow::keepView()
{
    m_viewTimer->stop();
    if (!m_document || m_autoFrame || !m_graph)
        return; // not touched by the user: nothing of theirs to keep
    const QPointF center = m_graph->viewCenter();
    const doc::GraphView view{true, m_graph->viewZoom(), center.x(), center.y()};
    if (view != m_document->compositingView())
        m_document->setCompositingView(view);
}

void CompositingWindow::scheduleRefresh()
{
    if (isVisible())
        m_timer->start();
}

void CompositingWindow::editMask(compositing::NodeId id)
{
    const compositing::Node *node = id ? m_network.node(id) : nullptr;
    const compositing::NodeId mask = node && node->type == QLatin1String("mask") ? id : 0;
    if (mask == m_editing)
        return;
    m_editing = mask;
    m_maskBar->setVisible(mask != 0);
    m_preview->setTool(mask ? ImagePreview::Tool::Rectangle : ImagePreview::Tool::Pan);
    for (auto *button : m_maskBar->findChildren<QToolButton *>()) // the bar starts at the Rectangle tool
        if (button->isCheckable())
            button->setChecked(button->text() == tr("Rectangle"));
    refresh();
}

QList<MaskShape> CompositingWindow::maskShapes() const
{
    const compositing::Node *node = m_editing ? m_network.node(m_editing) : nullptr;
    return node ? parseMaskShapes(node->data) : QList<MaskShape>();
}

void CompositingWindow::setMaskShapes(const QList<MaskShape> &shapes)
{
    if (m_editing)
        m_network.setData(m_editing, maskShapesToJson(shapes));
}

void CompositingWindow::addMaskShape(const MaskShape &shape)
{
    // Drawn on the preview, kept in pixels of the document: the preview may be smaller.
    const double k = m_frameScale > 0 ? 1.0 / m_frameScale : 1.0;
    MaskShape stored = shape;
    stored.rect = QRectF(shape.rect.x() * k, shape.rect.y() * k, shape.rect.width() * k, shape.rect.height() * k);
    stored.polygon.clear();
    for (const QPointF &point : shape.polygon)
        stored.polygon << point * k;
    QList<MaskShape> shapes = maskShapes();
    shapes.append(stored);
    setMaskShapes(shapes);
}

void CompositingWindow::refresh()
{
    if (m_frameDirty && m_document) {
        m_frame = io::renderFrame(m_document->document(), m_document->currentFrame(), false);
        m_frameScale = 1.0;
        const int pageWidth = m_frame.width();
        // A preview needs no more than this; blurs get slow on big pages.
        constexpr QSize limit(1280, 720);
        if (m_frame.width() > limit.width() || m_frame.height() > limit.height())
            m_frame = m_frame.scaled(limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (pageWidth > 0)
            m_frameScale = double(m_frame.width()) / pageWidth;
        m_frameDirty = false;
    }
    compositing::EvalContext context;
    context.frame = m_frame;
    context.frameIndex = m_document ? m_document->currentFrame() : 0;
    context.scale = m_frameScale;
    if (m_document)
        context.paper = m_document->document().paperColor();

    if (m_editing && !m_network.node(m_editing))
        editMask(0); // the node was taken away (an undo)
    if (m_editing) {
        // The mask being drawn: what goes into the node, with its areas tinted over it.
        QImage base = m_evaluator.evaluateInput(m_network, context, m_editing, 0);
        if (base.isNull())
            base = m_frame;
        const QImage matte = m_evaluator.evaluateNode(m_network, context, m_editing, 1);
        QImage tint;
        if (!matte.isNull()) {
            tint = QImage(matte.size(), QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < matte.height(); ++y) {
                const uchar *m = matte.constScanLine(y);
                QRgb *out = reinterpret_cast<QRgb *>(tint.scanLine(y));
                for (int x = 0; x < matte.width(); ++x) {
                    const int a = m[x] * 110 / 255; // red, see-through
                    out[x] = qRgba(a, a * 60 / 255, a * 60 / 255, a);
                }
            }
        }
        m_preview->setImage(base);
        m_preview->setOverlay(tint);
        return;
    }
    m_preview->setOverlay({});
    m_preview->setImage(m_evaluator.evaluate(m_network, context));
}

} // namespace mothion::ui
