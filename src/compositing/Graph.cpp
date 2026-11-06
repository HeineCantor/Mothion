#include "compositing/Graph.h"

#include "compositing/NodeType.h"

#include <algorithm>

namespace mothion::compositing {

NodeId Graph::addNode(const QString &type, const QPointF &pos)
{
    const NodeType *info = NodeRegistry::instance().find(type);
    if (!info)
        return 0;
    Node node;
    node.id = m_nextId++;
    node.type = type;
    node.pos = pos;
    node.stamp = ++m_stamp;
    for (const ParamInfo &param : info->params())
        node.params[param.key] = param.initial;
    m_nodes.push_back(node);
    notify({GraphChange::Kind::NodeAdded, node.id, {}});
    return node.id;
}

void Graph::removeNode(NodeId id)
{
    if (!node(id))
        return;
    // The wires first: whoever listens sees the node without them, never wires into nothing.
    bool linksChanged = false;
    for (size_t i = m_links.size(); i-- > 0;) {
        const Link link = m_links[i];
        if (link.from == id || link.to == id) {
            m_links.erase(m_links.begin() + static_cast<std::ptrdiff_t>(i));
            if (link.to != id)
                mutableNode(link.to)->stamp = ++m_stamp; // lost an input
            linksChanged = true;
        }
    }
    if (linksChanged)
        notify({GraphChange::Kind::Links, 0, {}});
    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [id](const Node &n) { return n.id == id; }),
                  m_nodes.end());
    notify({GraphChange::Kind::NodeRemoved, id, {}});
}

bool Graph::dependsOn(NodeId start, NodeId on) const
{
    std::vector<NodeId> stack{start};
    std::vector<NodeId> seen;
    while (!stack.empty()) {
        const NodeId current = stack.back();
        stack.pop_back();
        if (current == on)
            return true;
        if (std::find(seen.begin(), seen.end(), current) != seen.end())
            continue;
        seen.push_back(current);
        for (const Link &link : m_links)
            if (link.to == current)
                stack.push_back(link.from);
    }
    return false;
}

bool Graph::connect(NodeId from, int fromPort, NodeId to, int toPort)
{
    const Node *a = node(from);
    const Node *b = node(to);
    if (!a || !b || from == to)
        return false;
    const NodeType *outType = NodeRegistry::instance().find(a->type);
    const NodeType *inType = NodeRegistry::instance().find(b->type);
    if (!outType || !inType || fromPort < 0 || fromPort >= outType->outputs().size() || toPort < 0
        || toPort >= inType->inputs().size())
        return false;
    if (dependsOn(from, to)) // `to` already feeds `from`: this would close a loop
        return false;
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [&](const Link &l) { return l.to == to && l.toPort == toPort; }),
                  m_links.end());
    m_links.push_back({from, fromPort, to, toPort});
    mutableNode(to)->stamp = ++m_stamp;
    notify({GraphChange::Kind::Links, 0, {}});
    return true;
}

void Graph::disconnect(NodeId to, int toPort)
{
    const auto it = std::find_if(m_links.begin(), m_links.end(),
                                 [&](const Link &l) { return l.to == to && l.toPort == toPort; });
    if (it == m_links.end())
        return;
    m_links.erase(it);
    mutableNode(to)->stamp = ++m_stamp;
    notify({GraphChange::Kind::Links, 0, {}});
}

void Graph::setParam(NodeId id, const QString &key, const QVariant &value)
{
    Node *n = mutableNode(id);
    if (!n || !n->params.contains(key) || n->params.value(key) == value)
        return;
    n->params[key] = value;
    n->stamp = ++m_stamp;
    notify({GraphChange::Kind::Param, id, key});
}

void Graph::setPosition(NodeId id, const QPointF &pos)
{
    if (Node *n = mutableNode(id))
        n->pos = pos;
}

const Node *Graph::node(NodeId id) const
{
    for (const Node &n : m_nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

Node *Graph::mutableNode(NodeId id)
{
    return const_cast<Node *>(static_cast<const Graph *>(this)->node(id));
}

const Link *Graph::linkInto(NodeId to, int toPort) const
{
    for (const Link &l : m_links)
        if (l.to == to && l.toPort == toPort)
            return &l;
    return nullptr;
}

NodeId Graph::outputNode() const
{
    for (const Node &n : m_nodes)
        if (n.type == QLatin1String("output"))
            return n.id;
    return 0;
}

int Graph::addListener(Listener listener)
{
    m_listeners.emplace_back(m_nextToken, std::move(listener));
    return m_nextToken++;
}

void Graph::removeListener(int token)
{
    m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(),
                                     [token](const auto &entry) { return entry.first == token; }),
                      m_listeners.end());
}

void Graph::notify(const GraphChange &change)
{
    const auto listeners = m_listeners; // a listener may add or remove listeners
    for (const auto &entry : listeners)
        entry.second(change);
}

} // namespace mothion::compositing
