#include "ui/compositing/CompositingWindow.h"

#include "document/DocumentController.h"
#include "io/FrameExport.h"
#include "ui/compositing/ImagePreview.h"
#include "ui/compositing/NodeGraphView.h"

#include <QLabel>
#include <QPainter>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace mothion::ui {

CompositingWindow::CompositingWindow(document::DocumentController *document, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_document(document)
    , m_graph(nullptr)
    , m_preview(new ImagePreview(this))
    , m_timer(new QTimer(this))
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

    auto *hint = new QLabel(tr("Prototype: right-click to add a node, drag from a socket to connect, "
                               "Delete removes, middle button pans, wheel zooms, F frames all. In the picture above: drag to move, wheel to zoom, double click to fit."),
                            this);
    hint->setContentsMargins(8, 4, 8, 4);
    hint->setEnabled(false);

    auto *lower = new QWidget(this);
    auto *lowerLayout = new QVBoxLayout(lower);
    lowerLayout->setContentsMargins(0, 0, 0, 0);
    lowerLayout->setSpacing(0);
    lowerLayout->addWidget(hint);
    lowerLayout->addWidget(m_graph, 1);

    auto *split = new QSplitter(Qt::Vertical, this);
    m_split = split;
    split->setChildrenCollapsible(false);
    split->addWidget(m_preview);
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
    connect(m_graph, &NodeGraphView::viewTouched, this, [this] { m_autoFrame = false; });
    connect(split, &QSplitter::splitterMoved, this, [this] { m_autoFrame = false; });
    refresh();
}

void CompositingWindow::seed()
{
    // A frame through a blur, merged with a color.
    const compositing::NodeId source = m_network.addNode(QStringLiteral("frame"), {-330, -70});
    const compositing::NodeId blur = m_network.addNode(QStringLiteral("blur"), {-90, -110});
    const compositing::NodeId color = m_network.addNode(QStringLiteral("color"), {-330, 60});
    const compositing::NodeId merge = m_network.addNode(QStringLiteral("merge"), {160, -40});
    const compositing::NodeId out = m_network.addNode(QStringLiteral("output"), {400, -30});
    m_network.connect(source, 0, blur, 0);
    m_network.connect(blur, 0, merge, 0);
    m_network.connect(color, 0, merge, 1);
    m_network.connect(merge, 0, out, 0);
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
    // Half and half, the nodes zoomed to take 80% of the width.
    const int half = m_split->height() / 2;
    m_split->setSizes({half, m_split->height() - half});
    m_graph->fitWidth(0.8);
}

void CompositingWindow::scheduleRefresh()
{
    if (isVisible())
        m_timer->start();
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
    m_preview->setImage(m_evaluator.evaluate(m_network, context));
}

} // namespace mothion::ui
