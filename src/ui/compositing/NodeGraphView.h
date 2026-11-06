#pragma once

#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QHash>
#include <QImage>
#include <QVariant>
#include <functional>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace mothion::ui {

class NodeItem;
class EdgeItem;

// What a node does is not decided yet (this is the prototype of the interface):
// a kind is a title, a tint and the names of its inputs and outputs.
// A setting of a node, shown inside the node: a slider (an integer from min to max) or a color.
struct NodeParam
{
    enum class Kind { Slider, Color };

    QString key;
    QString label;
    Kind kind = Kind::Slider;
    int min = 0;
    int max = 100;
    QVariant initial; // an int, or a QColor
};

struct NodeKind
{
    enum class Type { Frame, Blur, Merge, Color, Output };

    Type type;
    QString title;
    QColor tint;
    QStringList inputs;
    QStringList outputs;
    QList<NodeParam> params;
};

// A box with its sockets: inputs on the left, outputs on the right.
class NodeItem : public QGraphicsObject
{
    Q_OBJECT
public:
    explicit NodeItem(const NodeKind &kind);

    const NodeKind &kind() const { return m_kind; }
    // The value of a setting (an int or a QColor), and changing it (the widget in the node follows).
    QVariant param(const QString &key) const { return m_values.value(key); }
    void setParam(const QString &key, const QVariant &value);
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

    // Where socket `index` sits, in scene coordinates.
    QPointF socketPos(bool output, int index) const;
    // The socket nearest a scene point, if one is within reach to connect to: true, which side and its index.
    // `onlySide` limits it to the inputs (0) or the outputs (1); `distance` gets how far it is.
    bool socketAt(const QPointF &scenePos, bool *output, int *index, int onlySide = -1,
                  qreal *distance = nullptr) const;
    // The socket drawn with an outline because the pen is close enough to connect (none: index -1).
    void setHotSocket(bool output, int index);
    QPainterPath shape() const override;
    int hotSocket() const { return m_hotIndex; }

    void addEdge(EdgeItem *edge) { m_edges.append(edge); }
    void removeEdge(EdgeItem *edge) { m_edges.removeOne(edge); }
    const QList<EdgeItem *> &edges() const { return m_edges; }

signals:
    void paramChanged();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QPointF socketLocal(bool output, int index) const;
    qreal bodyHeight() const;

    NodeKind m_kind;
    QList<EdgeItem *> m_edges;
    QVariantMap m_values;
    QHash<QString, std::function<void(const QVariant &)>> m_shown; // key -> updates its widget
    bool m_hotOutput = false;
    int m_hotIndex = -1;
};

// A curve from an output to an input. While it is being dragged one end is free.
class EdgeItem : public QGraphicsPathItem
{
public:
    EdgeItem();
    ~EdgeItem() override;

    void connect(NodeItem *from, int fromIndex, NodeItem *to, int toIndex);
    void setFreeEnd(const QPointF &scenePos) { m_free = scenePos; updatePath(); }
    void updatePath();

    QPointer<NodeItem> from; // guarded: the scene deletes nodes and wires in any order
    int fromIndex = 0;
    QPointer<NodeItem> to;
    int toIndex = 0;

protected:
    void paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *w) override;

private:
    QPointF m_free;
};

// The canvas of the node editor: pan with the middle button, zoom with the wheel,
// drag from a socket to connect, Delete removes what is selected, the context
// menu adds nodes.
class NodeGraphView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit NodeGraphView(QWidget *parent = nullptr);

    NodeItem *addNode(const NodeKind &kind, const QPointF &scenePos);
    bool connectNodes(NodeItem *from, int out, NodeItem *to, int in);
    static QList<NodeKind> kinds();

    // What the Output node ends up with when the Frame nodes hold `frame`;
    // a null image if the Output has nothing connected.
    QImage evaluate(const QImage &frame) const;

    int nodeCount() const;
    int edgeCount() const;
    // Frames the whole graph.
    void fitAll();
    // Zooms so the graph is `fraction` of the view wide (less if it would not fit in height), centered.
    void fitWidth(qreal fraction);

signals:
    void graphChanged();
    // The user panned, zoomed or clicked in the view: it is not to be framed again by itself.
    void viewTouched();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void drawForeground(QPainter *p, const QRectF &rect) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void drawBackground(QPainter *p, const QRectF &rect) override;

private:
    void deleteSelection();
    void beginWire(NodeItem *node, bool output, int index, const QPointF &scenePos);
    // The node whose socket is nearest the point, within reach, and which socket it is.
    NodeItem *socketNear(const QPointF &scenePos, bool *output, int *index, int onlySide,
                         const NodeItem *skip = nullptr) const;
    void setHot(NodeItem *node, bool output, int index);

    QGraphicsScene *m_scene;
    EdgeItem *m_dragEdge = nullptr; // the line being drawn from a socket
    NodeItem *m_dragNode = nullptr;
    bool m_dragFromOutput = false;
    int m_dragSocket = 0;
    QPointer<NodeItem> m_hotNode;
    bool m_cutting = false; // shift + drag: the wires the line crosses are cut
    QList<QPointF> m_cutLine;
    bool m_panning = false;
    QPoint m_panFrom;
};

} // namespace mothion::ui
