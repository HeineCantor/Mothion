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
    Done done;
    QImage result;
    if (const NodeId output = graph.outputNode())
        result = compute(graph, output, context, done).images.value(0);
    keep(graph, done);
    return result;
}

QImage Evaluator::evaluateNode(const Graph &graph, const EvalContext &context, NodeId id, int port)
{
    m_processed = 0;
    Done done;
    const QImage result = compute(graph, id, context, done).images.value(port);
    keep(graph, done);
    return result;
}

QImage Evaluator::evaluateInput(const Graph &graph, const EvalContext &context, NodeId id, int port)
{
    m_processed = 0;
    Done done;
    const Link *link = graph.linkInto(id, port);
    const QImage result = link ? compute(graph, link->from, context, done).images.value(link->fromPort) : QImage();
    keep(graph, done);
    return result;
}

void Evaluator::keep(const Graph &graph, Done &done)
{
    for (auto &[id, result] : done)
        m_kept[id] = std::move(result);
    for (auto it = m_kept.begin(); it != m_kept.end();)
        it = graph.node(it->first) ? std::next(it) : m_kept.erase(it);
}

Evaluator::Result Evaluator::compute(const Graph &graph, NodeId id, const EvalContext &context,
                                     Done &done)
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
    combine(signature, static_cast<quint64>(context.scale * 1e6));
    combine(signature, static_cast<quint64>(context.paper));
    if (type->dependsOnFrame())
        combine(signature, static_cast<quint64>(context.frame.cacheKey()));
    if (type->usesFrameIndex(node->params))
        combine(signature, static_cast<quint64>(context.frameIndex));
    for (int port = 0; port < type->inputs().size(); ++port) {
        const Link *link = graph.linkInto(id, port);
        const Result upstream = link ? compute(graph, link->from, context, done) : Result{};
        inputs.append(upstream.images.value(link ? link->fromPort : 0));
        combine(signature, upstream.signature);
    }

    Result result;
    result.signature = signature;
    if (const auto kept = m_kept.find(id); kept != m_kept.end() && kept->second.signature == signature) {
        result.images = kept->second.images;
    } else {
        QVariantMap settings = node->params;
        if (!node->data.isEmpty())
            settings[kDataKey] = node->data;
        result.images = type->processOutputs(inputs, settings, context);
        ++m_processed;
    }
    done[id] = result;
    return result;
}

} // namespace mothion::compositing
