#include "compositing/Evaluator.h"

namespace mothion::compositing {

namespace {

void combine(quint64 &hash, quint64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

} // namespace

QImage Evaluator::evaluate(const Graph &graph, const EvalContext &context)
{
    m_processed = 0;
    std::unordered_map<NodeId, Result> done;
    QImage result;
    if (const NodeId output = graph.outputNode())
        result = compute(graph, output, context, done).image;
    m_kept = std::move(done); // what was not reached this time (a removed node) is forgotten
    return result;
}

Evaluator::Result Evaluator::compute(const Graph &graph, NodeId id, const EvalContext &context,
                                     std::unordered_map<NodeId, Result> &done)
{
    if (const auto it = done.find(id); it != done.end())
        return it->second;
    const Node *node = graph.node(id);
    const NodeType *type = node ? NodeRegistry::instance().find(node->type) : nullptr;
    if (!type)
        return {};

    // The inputs first; they say, with the node's own stamp, whether what was kept still holds.
    QList<QImage> inputs;
    quint64 signature = static_cast<quint64>(id);
    combine(signature, node->stamp);
    combine(signature, static_cast<quint64>(context.frame.width()) << 32 | static_cast<quint64>(context.frame.height()));
    if (type->dependsOnFrame())
        combine(signature, static_cast<quint64>(context.frame.cacheKey()));
    for (int port = 0; port < type->inputs().size(); ++port) {
        const Link *link = graph.linkInto(id, port);
        const Result upstream = link ? compute(graph, link->from, context, done) : Result{};
        inputs.append(upstream.image);
        combine(signature, upstream.signature);
    }

    Result result;
    result.signature = signature;
    if (const auto kept = m_kept.find(id); kept != m_kept.end() && kept->second.signature == signature) {
        result.image = kept->second.image;
    } else {
        result.image = type->process(inputs, node->params, context);
        ++m_processed;
    }
    done[id] = result;
    return result;
}

} // namespace mothion::compositing
