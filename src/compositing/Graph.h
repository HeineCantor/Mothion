#pragma once

#include "core/document/Compositing.h"

#include <QPointF>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <vector>

namespace mothion::compositing {

using NodeId = int; // 1, 2, 3...; 0 is none

struct Node
{
    NodeId id = 0;
    QString type; // NodeType::id()
    QPointF pos;  // where the editor shows it
    QVariantMap params;
    quint64 stamp = 0; // changes whenever what the node computes may have changed
};

// The wire from output socket `fromPort` of `from` into input socket `toPort` of `to`.
struct Link
{
    NodeId from = 0;
    int fromPort = 0;
    NodeId to = 0;
    int toPort = 0;
};

struct GraphChange
{
    enum class Kind { NodeAdded, NodeRemoved, Links, Param, Moved, Reset };
    Kind kind;
    NodeId node = 0; // not for Links
    QString key;     // Param: the setting
    // Moved: a node was put somewhere else (nothing to compute anew). Reset: load() replaced everything.
};

// The compositing network: nodes, their settings and the wires between them. No pictures and no widgets,
// only the description of what is to be done: the Evaluator does it, the editor shows and changes it.
// An input socket takes one wire; wires never close a loop.
class Graph
{
public:
    using Listener = std::function<void(const GraphChange &)>;

    // Makes a node of the type called `type` with its settings at their start values; 0 if there is no such type.
    NodeId addNode(const QString &type, const QPointF &pos = {});
    // Takes the node out, with the wires that touch it.
    void removeNode(NodeId id);
    // A wire from an output to an input, replacing the one already into that input. False (nothing changes) if
    // the sockets do not exist, both are on one node, or the wire would close a loop.
    bool connect(NodeId from, int fromPort, NodeId to, int toPort);
    // Takes out the wire into an input, if there is one.
    void disconnect(NodeId to, int toPort);
    void setParam(NodeId id, const QString &key, const QVariant &value);
    void setPosition(NodeId id, const QPointF &pos);

    // The network as the document stores it, and the other way round: load() replaces everything with what
    // was stored (nodes of unknown types and wires that do not fit are dropped, missing settings start over),
    // and the listeners hear one Reset.
    doc::GraphData toData() const;
    void load(const doc::GraphData &data);

    const Node *node(NodeId id) const;
    const std::vector<Node> &nodes() const { return m_nodes; }
    const std::vector<Link> &links() const { return m_links; }
    const Link *linkInto(NodeId to, int toPort) const;
    NodeId outputNode() const; // the first Output node, or 0

    // Called after every change, from the thread that makes it. Returns a token for removeListener.
    int addListener(Listener listener);
    void removeListener(int token);

private:
    Node *mutableNode(NodeId id);
    void notify(const GraphChange &change);
    bool dependsOn(NodeId node, NodeId on) const; // `node` is fed, however far back, by `on`

    std::vector<Node> m_nodes;
    std::vector<Link> m_links;
    NodeId m_nextId = 1;
    quint64 m_stamp = 0;
    std::vector<std::pair<int, Listener>> m_listeners;
    int m_nextToken = 1;
};

} // namespace mothion::compositing
