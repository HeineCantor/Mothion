#pragma once

#include "compositing/Graph.h"
#include "compositing/NodeType.h"

#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QHash>
#include <QImage>
#include <QVariant>
#include <functional>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace mothion::ui {

class NodeItem;
class EdgeItem;

// What a node does is not decided yet (this is the prototype of the interface):
// a kind is a title, a tint and the names of its inputs and outputs.
// A box with its sockets: inputs on the left, outputs on the right.
class NodeItem : public QGraphicsObject
{
    Q_OBJECT
public:
    // The editor's picture of node `id` of `graph`; its settings are read from and written to the graph.
    NodeItem(compositing::Graph &graph, compositing::NodeId id);

    compositing::NodeId nodeId() const { return m_id; }
    const compositing::NodeType &nodeType() const { return *m_type; }
    // The widget of a setting takes the graph's value again (it was changed from elsewhere).
    void showParam(const QString &key);
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

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QPointF socketLocal(bool output, int index) const;
    qreal bodyHeight() const;

    compositing::Graph &m_graph;
    compositing::NodeId m_id;
    const compositing::NodeType *m_type;
    QList<EdgeItem *> m_edges;
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

// The canvas of the node editor, a view of a compositing::Graph (which must outlive it): pan with the middle
// button, zoom with the wheel, drag from a socket to connect, shift + drag cuts wires, Delete removes what is
// selected, the context menu adds nodes. Everything it changes goes through the graph.
class NodeGraphView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit NodeGraphView(compositing::Graph &graph, QWidget *parent = nullptr);
    ~NodeGraphView() override;

    compositing::Graph &graph() const { return m_graph; }
    NodeItem *item(compositing::NodeId id) const { return m_items.value(id); }
    compositing::NodeId addNode(const QString &type, const QPointF &scenePos);
    bool connectNodes(NodeItem *from, int out, NodeItem *to, int in);

    int nodeCount() const { return static_cast<int>(m_items.size()); }
    int edgeCount() const;
    // Frames the whole graph.
    void fitAll();
    // Zooms so the graph is `fraction` of the view wide (less if it would not fit in height), centered.
    void fitWidth(qreal fraction);

signals:
    // The user panned, zoomed or clicked in the view: it is not to be framed again by itself.
    void viewTouched();
    // A mouse button was released in the view: a drag, a slider or a wire is done (a good moment to keep the work).
    void gestureEnded();
    // The selection became a single node (its id), or anything else (0).
    void selectedNodeChanged(compositing::NodeId id);

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
    void graphChanged(const compositing::GraphChange &change);
    void addNodeItem(compositing::NodeId id);
    void removeEdges();  // the wires of the graph (not one being dragged)
    void rebuildEdges(); // the wires drawn are the graph's links
    void deleteSelection();
    void beginWire(NodeItem *node, bool output, int index, const QPointF &scenePos);
    // The node whose socket is nearest the point, within reach, and which socket it is.
    NodeItem *socketNear(const QPointF &scenePos, bool *output, int *index, int onlySide,
                         const NodeItem *skip = nullptr) const;
    void setHot(NodeItem *node, bool output, int index);

    compositing::Graph &m_graph;
    int m_listener = 0;
    QGraphicsScene *m_scene;
    QHash<compositing::NodeId, NodeItem *> m_items;
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
