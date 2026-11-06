#include "ui/compositing/NodeGraphView.h"

#include <QColorDialog>
#include <QContextMenuEvent>
#include <QGraphicsProxyWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionGraphicsItem>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

namespace mothion::ui {

namespace {

constexpr qreal kNodeWidth = 150;
constexpr qreal kHeaderHeight = 24;
constexpr qreal kRowHeight = 22;
constexpr qreal kSocketRadius = 5;
constexpr qreal kSocketGrab = 18; // how near the pen must be to connect
constexpr qreal kHotMargin = 10;
constexpr qreal kParamHeight = 28;

} // namespace

// ---- NodeItem -------------------------------------------------------------

NodeItem::NodeItem(const NodeKind &kind)
    : m_kind(kind)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(1);

    // The settings, as widgets under the sockets.
    const qreal rows = static_cast<qreal>(std::max<qsizetype>(1, std::max(m_kind.inputs.size(), m_kind.outputs.size())));
    qreal y = kHeaderHeight + 6 + rows * kRowHeight;
    for (const NodeParam &param : std::as_const(m_kind.params)) {
        m_values[param.key] = param.initial;
        auto *row = new QWidget;
        row->setAttribute(Qt::WA_NoSystemBackground);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 2, 0, 2);
        layout->setSpacing(6);
        auto *label = new QLabel(param.label, row);
        label->setMinimumWidth(48);
        layout->addWidget(label);
        const QString key = param.key;
        if (param.kind == NodeParam::Kind::Slider) {
            auto *slider = new QSlider(Qt::Horizontal, row);
            slider->setRange(param.min, param.max);
            slider->setValue(param.initial.toInt());
            auto *number = new QLabel(QString::number(param.initial.toInt()), row);
            number->setMinimumWidth(24);
            number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            layout->addWidget(slider, 1);
            layout->addWidget(number);
            connect(slider, &QSlider::valueChanged, this, [this, key, number](int v) {
                number->setText(QString::number(v));
                setParam(key, v);
            });
            m_shown[key] = [slider](const QVariant &v) {
                const QSignalBlocker block(slider);
                slider->setValue(v.toInt());
                static_cast<QLabel *>(slider->parentWidget()->layout()->itemAt(2)->widget())->setText(QString::number(v.toInt()));
            };
        } else {
            auto *swatch = new QPushButton(row);
            swatch->setFixedHeight(22);
            auto paint = [swatch](const QColor &c) {
                swatch->setStyleSheet(QStringLiteral("QPushButton { background-color: rgba(%1,%2,%3,%4); "
                                                     "border: 1px solid palette(mid); border-radius: 3px; }")
                                          .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
            };
            paint(param.initial.value<QColor>());
            layout->addWidget(swatch, 1);
            connect(swatch, &QPushButton::clicked, this, [this, key] {
                // The swatch lives inside the scene and has no window of its own: the dialog hangs on the view's.
                // It is opened without blocking (the picture follows the color as it is picked), with Qt's own
                // dialog: the system's can hang over a window like this one.
                const QList<QGraphicsView *> views = scene() ? scene()->views() : QList<QGraphicsView *>();
                const QColor original = m_values.value(key).value<QColor>();
                auto *dialog = new QColorDialog(original, views.isEmpty() ? nullptr : views.first()->window());
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->setOptions(QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
                dialog->setWindowTitle(tr("Color"));
                connect(dialog, &QColorDialog::currentColorChanged, this, [this, key](const QColor &c) { setParam(key, c); });
                connect(dialog, &QColorDialog::rejected, this, [this, key, original] { setParam(key, original); });
                dialog->open();
            });
            m_shown[key] = [paint](const QVariant &v) { paint(v.value<QColor>()); };
        }
        auto *proxy = new QGraphicsProxyWidget(this);
        proxy->setWidget(row);
        proxy->setGeometry(QRectF(8, y, kNodeWidth - 16, kParamHeight));
        y += kParamHeight;
    }
}

QRectF NodeItem::boundingRect() const
{
    return QRectF(-2, -2, kNodeWidth + 4, bodyHeight() + 4).adjusted(-kHotMargin, -kHotMargin, kHotMargin, kHotMargin);
}

qreal NodeItem::bodyHeight() const
{
    const qreal rows = static_cast<qreal>(std::max<qsizetype>(1, std::max(m_kind.inputs.size(), m_kind.outputs.size())));
    return kHeaderHeight + rows * kRowHeight + 6 + static_cast<qreal>(m_kind.params.size()) * kParamHeight;
}

void NodeItem::setParam(const QString &key, const QVariant &value)
{
    if (m_values.value(key) == value)
        return;
    m_values[key] = value;
    if (const auto shown = m_shown.value(key))
        shown(value);
    emit paramChanged();
}

QPointF NodeItem::socketLocal(bool output, int index) const
{
    return {output ? kNodeWidth : 0.0, kHeaderHeight + 6 + kRowHeight * (index + 0.5)};
}

QPointF NodeItem::socketPos(bool output, int index) const
{
    return mapToScene(socketLocal(output, index));
}

bool NodeItem::socketAt(const QPointF &scenePos, bool *output, int *index, int onlySide, qreal *distance) const
{
    const QPointF local = mapFromScene(scenePos);
    qreal best = kSocketGrab;
    bool found = false;
    for (int side = 0; side < 2; ++side) {
        if (onlySide >= 0 && side != onlySide)
            continue;
        const int count = static_cast<int>(side ? m_kind.outputs.size() : m_kind.inputs.size());
        for (int i = 0; i < count; ++i) {
            const QPointF d = local - socketLocal(side, i);
            const qreal dist = std::hypot(d.x(), d.y());
            if (dist <= best) {
                best = dist;
                *output = side;
                *index = i;
                found = true;
            }
        }
    }
    if (found && distance)
        *distance = best;
    return found;
}

void NodeItem::setHotSocket(bool output, int index)
{
    if (m_hotIndex == index && (index < 0 || m_hotOutput == output))
        return;
    m_hotOutput = output;
    m_hotIndex = index;
    update();
}

QPainterPath NodeItem::shape() const
{
    // The body only: the reach around the sockets is not for grabbing the node.
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, kNodeWidth, bodyHeight()), 6, 6);
    return path;
}

void NodeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *widget)
{
    const QPalette pal = widget ? widget->palette() : QPalette();
    const QRectF body(0, 0, kNodeWidth, bodyHeight());

    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, 50));
    p->drawRoundedRect(body.translated(2, 3), 6, 6); // soft shadow

    p->setBrush(pal.color(QPalette::Base).lighter(112));
    p->setPen(QPen(isSelected() ? pal.color(QPalette::Highlight) : pal.color(QPalette::Mid), isSelected() ? 2 : 1));
    p->drawRoundedRect(body, 6, 6);

    // The tinted header, rounded at the top only.
    QPainterPath head;
    head.addRoundedRect(QRectF(0, 0, kNodeWidth, kHeaderHeight + 6), 6, 6);
    QPainterPath cut;
    cut.addRect(QRectF(0, kHeaderHeight, kNodeWidth, 10));
    p->setPen(Qt::NoPen);
    p->setBrush(m_kind.tint);
    p->drawPath(head.subtracted(cut));
    p->setPen(Qt::white);
    QFont bold = p->font();
    bold.setBold(true);
    p->setFont(bold);
    p->drawText(QRectF(10, 0, kNodeWidth - 20, kHeaderHeight), Qt::AlignVCenter | Qt::AlignLeft, m_kind.title);
    p->setFont(widget ? widget->font() : QFont());

    for (int side = 0; side < 2; ++side) {
        const QStringList &names = side ? m_kind.outputs : m_kind.inputs;
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            const QPointF c = socketLocal(side, i);
            p->setPen(QPen(pal.color(QPalette::Base), 1.5));
            p->setBrush(m_kind.tint.lighter(130));
            p->drawEllipse(c, kSocketRadius, kSocketRadius);
            if (m_hotIndex == i && m_hotOutput == static_cast<bool>(side)) { // near enough to connect
                p->setPen(QPen(pal.color(QPalette::Highlight), 2));
                p->setBrush(Qt::NoBrush);
                p->drawEllipse(c, kSocketRadius + 3.5, kSocketRadius + 3.5);
            }
            p->setPen(pal.color(QPalette::Text));
            const QRectF label(side ? 8 : 14, c.y() - kRowHeight / 2, kNodeWidth - 22, kRowHeight);
            p->drawText(label, Qt::AlignVCenter | (side ? Qt::AlignRight : Qt::AlignLeft), names[i]);
        }
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged)
        for (EdgeItem *edge : std::as_const(m_edges))
            edge->updatePath();
    return QGraphicsObject::itemChange(change, value);
}

// ---- EdgeItem -------------------------------------------------------------

EdgeItem::EdgeItem()
{
    setZValue(0);
    setFlag(ItemIsSelectable);
    setAcceptedMouseButtons(Qt::LeftButton);
    setPen(QPen(Qt::black, 10)); // only for the shape: a wide strip to click or cut, easy with a pen
}

EdgeItem::~EdgeItem()
{
    if (from)
        from->removeEdge(this);
    if (to)
        to->removeEdge(this);
}

void EdgeItem::connect(NodeItem *f, int fi, NodeItem *t, int ti)
{
    from = f;
    fromIndex = fi;
    to = t;
    toIndex = ti;
    from->addEdge(this);
    to->addEdge(this);
    updatePath();
}

void EdgeItem::updatePath()
{
    const QPointF a = from ? from->socketPos(true, fromIndex) : m_free;
    const QPointF b = to ? to->socketPos(false, toIndex) : m_free;
    const qreal pull = std::max<qreal>(40, std::abs(b.x() - a.x()) * 0.5);
    QPainterPath path(a);
    path.cubicTo(a + QPointF(pull, 0), b - QPointF(pull, 0), b);
    prepareGeometryChange();
    setPath(path);
}

void EdgeItem::paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *w)
{
    const QPalette pal = w ? w->palette() : QPalette();
    const bool selected = option->state & QStyle::State_Selected;
    p->setRenderHint(QPainter::Antialiasing);
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(selected ? pal.color(QPalette::Highlight) : pal.color(QPalette::Text).darker(selected ? 100 : 130),
                   selected ? 3 : 2));
    p->drawPath(path());
}

// ---- NodeGraphView --------------------------------------------------------

NodeGraphView::NodeGraphView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    m_scene->setSceneRect(-5000, -5000, 10000, 10000);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setDragMode(RubberBandDrag);
    setViewportUpdateMode(FullViewportUpdate);

    // A small graph to look at: a frame through a blur, merged with a color.
    const QList<NodeKind> all = kinds();
    auto *source = addNode(all[0], {-330, -70});
    auto *blur = addNode(all[1], {-90, -110});
    auto *color = addNode(all[3], {-330, 60});
    auto *merge = addNode(all[2], {160, -40});
    auto *out = addNode(all[4], {400, -30});
    connectNodes(source, 0, blur, 0);
    connectNodes(blur, 0, merge, 0);
    connectNodes(color, 0, merge, 1);
    connectNodes(merge, 0, out, 0);
}

QList<NodeKind> NodeGraphView::kinds()
{
    return {
        {NodeKind::Type::Frame, tr("Frame"), QColor(70, 110, 170), {}, {tr("Image")}, {}},
        {NodeKind::Type::Blur, tr("Blur"), QColor(150, 90, 60), {tr("Image")}, {tr("Image")},
         {{QStringLiteral("radius"), tr("Radius"), NodeParam::Kind::Slider, 0, 60, 8}}},
        {NodeKind::Type::Merge, tr("Merge"), QColor(60, 130, 100), {tr("Back"), tr("Front")}, {tr("Image")},
         {{QStringLiteral("opacity"), tr("Opacity"), NodeParam::Kind::Slider, 0, 100, 100}}},
        {NodeKind::Type::Color, tr("Color"), QColor(140, 80, 140), {}, {tr("Image")},
         {{QStringLiteral("color"), tr("Color"), NodeParam::Kind::Color, 0, 0, QColor(150, 60, 190, 110)}}},
        {NodeKind::Type::Output, tr("Output"), QColor(150, 70, 80), {tr("Image")}, {}, {}},
    };
}

NodeItem *NodeGraphView::addNode(const NodeKind &kind, const QPointF &scenePos)
{
    auto *node = new NodeItem(kind);
    node->setPos(scenePos);
    m_scene->addItem(node);
    connect(node, &NodeItem::paramChanged, this, &NodeGraphView::graphChanged);
    emit graphChanged();
    return node;
}

bool NodeGraphView::connectNodes(NodeItem *from, int out, NodeItem *to, int in)
{
    if (!from || !to || from == to)
        return false;
    // An input takes one wire: a new one replaces the old.
    const QList<EdgeItem *> existing = to->edges();
    for (EdgeItem *edge : existing)
        if (edge->to == to && edge->toIndex == in)
            delete edge;
    // No loops: the source must not depend on the target.
    QList<NodeItem *> stack{from};
    QSet<NodeItem *> seen;
    while (!stack.isEmpty()) {
        NodeItem *n = stack.takeLast();
        if (n == to)
            return false;
        if (seen.contains(n))
            continue;
        seen.insert(n);
        for (EdgeItem *edge : n->edges())
            if (edge->to == n && edge->from)
                stack.append(edge->from);
    }
    auto *edge = new EdgeItem;
    m_scene->addItem(edge);
    edge->connect(from, out, to, in);
    emit graphChanged();
    return true;
}

namespace {

// A box blur of `radius` pixels, done as two running-sum passes.
QImage boxBlur(const QImage &source, int radius)
{
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (radius < 1 || image.isNull())
        return image;
    const int w = image.width(), h = image.height();
    QImage temp(w, h, image.format());
    const int window = radius * 2 + 1;
    auto pass = [&](const QImage &in, QImage &out, bool horizontal) {
        const int lines = horizontal ? h : w;
        const int length = horizontal ? w : h;
        for (int line = 0; line < lines; ++line) {
            auto at = [&](int i) {
                i = std::clamp(i, 0, length - 1);
                return horizontal ? reinterpret_cast<const QRgb *>(in.constScanLine(line))[i]
                                  : reinterpret_cast<const QRgb *>(in.constScanLine(i))[line];
            };
            int sum[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const QRgb c = at(i);
                sum[0] += qAlpha(c); sum[1] += qRed(c); sum[2] += qGreen(c); sum[3] += qBlue(c);
            }
            for (int i = 0; i < length; ++i) {
                const QRgb px = qRgba(sum[1] / window, sum[2] / window, sum[3] / window, sum[0] / window);
                if (horizontal)
                    reinterpret_cast<QRgb *>(out.scanLine(line))[i] = px;
                else
                    reinterpret_cast<QRgb *>(out.scanLine(i))[line] = px;
                const QRgb add = at(i + radius + 1), drop = at(i - radius);
                sum[0] += qAlpha(add) - qAlpha(drop); sum[1] += qRed(add) - qRed(drop);
                sum[2] += qGreen(add) - qGreen(drop); sum[3] += qBlue(add) - qBlue(drop);
            }
        }
    };
    pass(image, temp, true);
    pass(temp, image, false);
    return image;
}

} // namespace

QImage NodeGraphView::evaluate(const QImage &frame) const
{
    // The image a node's input socket receives: that of the output it is wired to.
    std::function<QImage(const NodeItem *, int)> input = [&](const NodeItem *node, int index) -> QImage {
        for (const EdgeItem *edge : node->edges())
            if (edge->to == node && edge->toIndex == index && edge->from) {
                const NodeItem *source = edge->from;
                const QImage first = source->kind().inputs.isEmpty() ? QImage() : input(source, 0);
                switch (source->kind().type) {
                case NodeKind::Type::Frame:
                    return frame;
                case NodeKind::Type::Blur:
                    return boxBlur(first, source->param(QStringLiteral("radius")).toInt());
                case NodeKind::Type::Color: {
                    QImage solid(frame.size(), QImage::Format_ARGB32_Premultiplied);
                    solid.fill(source->param(QStringLiteral("color")).value<QColor>());
                    return solid;
                }
                case NodeKind::Type::Merge: {
                    QImage back = first;
                    const QImage front = input(source, 1);
                    if (back.isNull())
                        return front;
                    back = back.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                    if (!front.isNull()) {
                        QPainter p(&back);
                        p.setOpacity(source->param(QStringLiteral("opacity")).toInt() / 100.0);
                        p.drawImage(QRect(QPoint(), back.size()), front);
                    }
                    return back;
                }
                case NodeKind::Type::Output:
                    return {};
                }
            }
        return {};
    };
    for (QGraphicsItem *item : m_scene->items())
        if (auto *node = dynamic_cast<NodeItem *>(item); node && node->kind().type == NodeKind::Type::Output)
            return input(node, 0);
    return {};
}

int NodeGraphView::nodeCount() const
{
    int n = 0;
    for (QGraphicsItem *item : m_scene->items())
        n += qgraphicsitem_cast<QGraphicsObject *>(item) && dynamic_cast<NodeItem *>(item);
    return n;
}

int NodeGraphView::edgeCount() const
{
    int n = 0;
    for (QGraphicsItem *item : m_scene->items())
        n += dynamic_cast<EdgeItem *>(item) && static_cast<EdgeItem *>(item)->to;
    return n;
}

void NodeGraphView::fitWidth(qreal fraction)
{
    const QRectF box = m_scene->itemsBoundingRect();
    if (box.isEmpty() || viewport()->width() < 50)
        return;
    const qreal byWidth = viewport()->width() * fraction / box.width();
    const qreal byHeight = viewport()->height() * 0.95 / box.height();
    const qreal s = std::clamp(std::min(byWidth, byHeight), 0.2, 3.0);
    resetTransform();
    scale(s, s);
    centerOn(box.center());
}

void NodeGraphView::fitAll()
{
    const QRectF box = m_scene->itemsBoundingRect();
    if (!box.isEmpty())
        fitInView(box.adjusted(-60, -60, 60, 60), Qt::KeepAspectRatio);
}

NodeItem *NodeGraphView::socketNear(const QPointF &scenePos, bool *output, int *index, int onlySide,
                                    const NodeItem *skip) const
{
    NodeItem *best = nullptr;
    qreal bestDistance = 1e9;
    const QRectF around(scenePos - QPointF(kSocketGrab + 2, kSocketGrab + 2), QSizeF(2 * kSocketGrab + 4, 2 * kSocketGrab + 4));
    for (QGraphicsItem *item : m_scene->items(around)) {
        auto *node = dynamic_cast<NodeItem *>(item);
        bool o = false;
        int i = 0;
        qreal d = 0;
        if (node && node != skip && node->socketAt(scenePos, &o, &i, onlySide, &d) && d < bestDistance) {
            best = node;
            bestDistance = d;
            *output = o;
            *index = i;
        }
    }
    return best;
}

void NodeGraphView::setHot(NodeItem *node, bool output, int index)
{
    if (m_hotNode && m_hotNode != node)
        m_hotNode->setHotSocket(false, -1);
    m_hotNode = node;
    if (node)
        node->setHotSocket(output, index);
}

void NodeGraphView::beginWire(NodeItem *node, bool output, int index, const QPointF &scenePos)
{
    m_dragNode = node;
    m_dragFromOutput = output;
    m_dragSocket = index;
    m_dragEdge = new EdgeItem;
    m_scene->addItem(m_dragEdge);
    // From an output; from an input, the wire is drawn backwards.
    if (output) {
        m_dragEdge->from = node;
        m_dragEdge->fromIndex = index;
    } else {
        m_dragEdge->to = node;
        m_dragEdge->toIndex = index;
    }
    m_dragEdge->setFreeEnd(scenePos);
}

void NodeGraphView::mousePressEvent(QMouseEvent *event)
{
    emit viewTouched();
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panFrom = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        bool output = false;
        int index = 0;
        if (NodeItem *node = socketNear(scenePos, &output, &index, -1)) {
            // A socket with a wire into it: the wire is picked up by its end and goes on from its source;
            // let go over nothing and it is gone.
            if (!output) {
                EdgeItem *wire = nullptr;
                for (EdgeItem *edge : node->edges())
                    if (edge->to == node && edge->toIndex == index && edge->from)
                        wire = edge;
                if (wire) {
                    node = wire->from;
                    index = wire->fromIndex;
                    output = true;
                    delete wire;
                    emit graphChanged();
                }
            }
            setHot(nullptr, false, -1);
            beginWire(node, output, index, scenePos);
            return;
        }
        if (event->modifiers() & Qt::ShiftModifier) {
            m_cutting = true;
            m_cutLine = {scenePos};
            setCursor(Qt::CrossCursor);
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void NodeGraphView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint d = event->pos() - m_panFrom;
        m_panFrom = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        return;
    }
    const QPointF scenePos = mapToScene(event->pos());
    if (m_cutting) {
        m_cutLine.append(scenePos);
        viewport()->update();
        return;
    }
    if (m_dragEdge) {
        m_dragEdge->setFreeEnd(scenePos);
        bool output = false;
        int index = 0;
        // Only the other kind of socket, on another node, can take the wire.
        NodeItem *near = socketNear(scenePos, &output, &index, m_dragFromOutput ? 0 : 1, m_dragNode);
        setHot(near, output, index);
        return;
    }
    if (event->buttons() == Qt::NoButton) {
        bool output = false;
        int index = 0;
        NodeItem *near = socketNear(scenePos, &output, &index, -1);
        setHot(near, output, index);
    }
    QGraphicsView::mouseMoveEvent(event);
}

void NodeGraphView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        unsetCursor();
        return;
    }
    if (m_cutting && event->button() == Qt::LeftButton) {
        m_cutting = false;
        unsetCursor();
        QPainterPath line(m_cutLine.first());
        for (const QPointF &point : std::as_const(m_cutLine))
            line.lineTo(point);
        QPainterPathStroker stroker;
        stroker.setWidth(2);
        const QPainterPath strip = stroker.createStroke(line);
        QList<EdgeItem *> cut;
        for (QGraphicsItem *item : m_scene->items(strip.boundingRect()))
            if (auto *edge = dynamic_cast<EdgeItem *>(item); edge && edge->to && edge->from && edge->shape().intersects(strip))
                cut.append(edge);
        qDeleteAll(cut);
        m_cutLine.clear();
        viewport()->update();
        if (!cut.isEmpty())
            emit graphChanged();
        return;
    }
    if (m_dragEdge && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        NodeItem *start = m_dragNode;
        const bool fromOutput = m_dragFromOutput;
        const int startSocket = m_dragSocket;
        delete m_dragEdge; // the temporary line; the real wire is made below
        m_dragEdge = nullptr;
        m_dragNode = nullptr;
        setHot(nullptr, false, -1);
        bool output = false;
        int index = 0;
        if (NodeItem *target = socketNear(scenePos, &output, &index, fromOutput ? 0 : 1, start)) {
            if (fromOutput)
                connectNodes(start, startSocket, target, index);
            else
                connectNodes(target, index, start, startSocket);
        }
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void NodeGraphView::leaveEvent(QEvent *event)
{
    setHot(nullptr, false, -1);
    QGraphicsView::leaveEvent(event);
}

void NodeGraphView::drawForeground(QPainter *p, const QRectF &rect)
{
    QGraphicsView::drawForeground(p, rect);
    if (!m_cutting || m_cutLine.size() < 2)
        return;
    QPainterPath line(m_cutLine.first());
    for (const QPointF &point : std::as_const(m_cutLine))
        line.lineTo(point);
    p->setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(230, 70, 70), 2, Qt::DashLine);
    pen.setCosmetic(true);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->drawPath(line);
}

void NodeGraphView::wheelEvent(QWheelEvent *event)
{
    emit viewTouched();
    const qreal step = std::pow(1.0015, event->angleDelta().y());
    const qreal next = std::clamp(transform().m11() * step, 0.2, 3.0);
    const qreal factor = next / transform().m11();
    scale(factor, factor);
}

void NodeGraphView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelection();
        return;
    }
    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_F) {
        fitAll();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void NodeGraphView::deleteSelection()
{
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *item : selected) {
        if (auto *node = dynamic_cast<NodeItem *>(item)) {
            const QList<EdgeItem *> edges = node->edges();
            for (EdgeItem *edge : edges)
                delete edge;
        }
    }
    // Edges may be gone with their nodes: look the rest up again.
    for (QGraphicsItem *item : m_scene->selectedItems())
        delete item;
    emit graphChanged();
}

void NodeGraphView::contextMenuEvent(QContextMenuEvent *event)
{
    const QPointF scenePos = mapToScene(event->pos());
    QMenu menu(this);
    menu.addSection(tr("Add node"));
    const QList<NodeKind> all = kinds();
    for (const NodeKind &kind : all)
        menu.addAction(kind.title)->setData(kind.title);
    if (QAction *chosen = menu.exec(event->globalPos()))
        for (const NodeKind &kind : all)
            if (kind.title == chosen->data().toString())
                addNode(kind, scenePos)->setSelected(true);
}

void NodeGraphView::drawBackground(QPainter *p, const QRectF &rect)
{
    const QColor base = palette().color(QPalette::Window);
    p->fillRect(rect, base.lightness() < 128 ? base.darker(130) : base.darker(106));
    const QColor line = palette().color(QPalette::Mid);
    constexpr int grid = 24;
    QVector<QLineF> minor, major;
    for (qreal x = std::floor(rect.left() / grid) * grid; x < rect.right(); x += grid)
        (std::lround(x / grid) % 5 ? minor : major).append(QLineF(x, rect.top(), x, rect.bottom()));
    for (qreal y = std::floor(rect.top() / grid) * grid; y < rect.bottom(); y += grid)
        (std::lround(y / grid) % 5 ? minor : major).append(QLineF(rect.left(), y, rect.right(), y));
    QColor faint = line;
    faint.setAlpha(40);
    p->setPen(faint);
    p->drawLines(minor);
    faint.setAlpha(90);
    p->setPen(faint);
    p->drawLines(major);
}

} // namespace mothion::ui
