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

void Graph::setData(NodeId id, const QString &data)
{
    Node *n = mutableNode(id);
    if (!n || n->data == data)
        return;
    n->data = data;
    n->stamp = ++m_stamp;
    notify({GraphChange::Kind::Data, id, {}});
}

void Graph::setPosition(NodeId id, const QPointF &pos)
{
    Node *n = mutableNode(id);
    if (!n || n->pos == pos)
        return;
    n->pos = pos;
    notify({GraphChange::Kind::Moved, id, {}});
}

doc::GraphData Graph::toData() const
{
    doc::GraphData data;
    data.nextId = m_nextId;
    for (const Node &n : m_nodes) {
        doc::GraphNodeData node;
        node.id = n.id;
        node.type = n.type.toStdString();
        node.x = n.pos.x();
        node.y = n.pos.y();
        node.data = n.data.toStdString();
        if (const NodeType *type = NodeRegistry::instance().find(n.type))
            for (const ParamInfo &param : type->params()) {
                const QVariant value = n.params.value(param.key);
                node.params.emplace_back(param.key.toStdString(),
                                         param.kind == ParamInfo::Kind::Color ? static_cast<int>(value.value<QColor>().rgba())
                                                                              : value.toInt());
            }
        data.nodes.push_back(std::move(node));
    }
    for (const Link &l : m_links)
        data.links.push_back({l.from, l.fromPort, l.to, l.toPort});
    return data;
}

void Graph::load(const doc::GraphData &data)
{
    const auto listeners = std::move(m_listeners); // quiet while it is rebuilt: one Reset at the end
    m_listeners.clear();
    m_nodes.clear();
    m_links.clear();
    m_nextId = std::max(1, data.nextId);
    for (const doc::GraphNodeData &stored : data.nodes) {
        const QString typeName = QString::fromStdString(stored.type);
        const NodeType *type = NodeRegistry::instance().find(typeName);
        if (!type || stored.id < 1 || node(stored.id))
            continue;
        Node n;
        n.id = stored.id;
        n.type = typeName;
        n.pos = {stored.x, stored.y};
        n.data = QString::fromStdString(stored.data);
        n.stamp = ++m_stamp;
        for (const ParamInfo &param : type->params()) {
            n.params[param.key] = param.initial;
            for (const auto &[key, value] : stored.params)
                if (QString::fromStdString(key) == param.key)
                    n.params[param.key] = param.kind == ParamInfo::Kind::Color
                                              ? QVariant::fromValue(QColor::fromRgba(static_cast<QRgb>(value)))
                                              : QVariant(std::clamp(value, param.min, param.max));
        }
        m_nodes.push_back(std::move(n));
        m_nextId = std::max(m_nextId, stored.id + 1);
    }
    for (const doc::GraphLinkData &l : data.links)
        connect(l.from, l.fromPort, l.to, l.toPort); // checks the sockets and loops
    m_listeners = listeners;
    notify({GraphChange::Kind::Reset, 0, {}});
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
