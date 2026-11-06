#include "ui/compositing/CompositingWindow.h"

#include "document/DocumentController.h"
#include "io/FrameExport.h"
#include "ui/compositing/NodeGraphView.h"

#include <QLabel>
#include <QPainter>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

namespace mothion::ui {

// Paints an image scaled to fit, centered on the desk color.
class ImagePreview : public QWidget
{
public:
    explicit ImagePreview(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setImage(const QImage &image)
    {
        m_image = image;
        update();
    }
    const QImage &image() const { return m_image; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QColor window = palette().color(QPalette::Window);
        p.fillRect(rect(), window.lightness() < 128 ? window.lighter(135) : window.darker(115));
        if (m_image.isNull()) {
            p.setPen(palette().color(QPalette::PlaceholderText));
            p.drawText(rect(), Qt::AlignCenter, tr("Nothing connected to Output"));
            return;
        }
        const QSize fitted = m_image.size().scaled(size() - QSize(24, 24), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(target, m_image);
    }

private:
    QImage m_image;
};

CompositingWindow::CompositingWindow(document::DocumentController *document, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_document(document)
    , m_graph(nullptr)
    , m_preview(new ImagePreview(this))
    , m_timer(new QTimer(this))
    , m_frameTimer(new QTimer(this))
{
    // A small network to look at: a frame through a blur, merged with a color.
    namespace nodes = compositing;
    const nodes::NodeId source = m_network.addNode(QStringLiteral("frame"), {-330, -70});
    const nodes::NodeId blur = m_network.addNode(QStringLiteral("blur"), {-90, -110});
    const nodes::NodeId color = m_network.addNode(QStringLiteral("color"), {-330, 60});
    const nodes::NodeId merge = m_network.addNode(QStringLiteral("merge"), {160, -40});
    const nodes::NodeId out = m_network.addNode(QStringLiteral("output"), {400, -30});
    m_network.connect(source, 0, blur, 0);
    m_network.connect(blur, 0, merge, 0);
    m_network.connect(color, 0, merge, 1);
    m_network.connect(merge, 0, out, 0);
    m_graph = new NodeGraphView(m_network, this);

    setWindowTitle(tr("Compositing"));
    resize(1100, 700); // what restoring from maximized gives

    auto *hint = new QLabel(tr("Prototype: right-click to add a node, drag from a socket to connect, "
                               "Delete removes, middle button pans, wheel zooms, F frames all."),
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
    m_listener = m_network.addListener([this](const compositing::GraphChange &) { scheduleRefresh(); });
    if (m_document) {
        connect(m_document, &document::DocumentController::currentFrameChanged, this, &CompositingWindow::scheduleRefresh);
        connect(m_document, &document::DocumentController::structureChanged, this, &CompositingWindow::scheduleRefresh);
        connect(m_document, &document::DocumentController::pixelsChanged, this, &CompositingWindow::scheduleRefresh);
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

CompositingWindow::~CompositingWindow()
{
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
    QImage frame;
    if (m_document) {
        frame = io::renderFrame(m_document->document(), m_document->currentFrame(), false);
        // A preview needs no more than this; blurs get slow on big pages.
        constexpr QSize limit(1280, 720);
        if (frame.width() > limit.width() || frame.height() > limit.height())
            frame = frame.scaled(limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    m_preview->setImage(m_evaluator.evaluate(m_network, {frame}));
}

} // namespace mothion::ui
