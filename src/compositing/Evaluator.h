#pragma once

#include "compositing/Graph.h"
#include "compositing/NodeType.h"

#include <unordered_map>

namespace mothion::compositing {

// Runs a Graph: from a node back along the wires, each node processes what its inputs give.
// What a node produced is kept, and used again while nothing it depends on has changed (its settings,
// its wires, the nodes upstream, and for those that look at it, the frame): editing one node only
// computes it and what comes after it. Several questions in a row (the result, and what goes into one
// node) share what is kept.
class Evaluator
{
public:
    // The image that reaches the Output node: null if the graph has no Output or nothing is wired to it.
    QImage evaluate(const Graph &graph, const EvalContext &context);
    // The image that comes out of output socket `port` of a node, and the one that goes into its input socket.
    QImage evaluateNode(const Graph &graph, const EvalContext &context, NodeId id, int port = 0);
    QImage evaluateInput(const Graph &graph, const EvalContext &context, NodeId id, int port = 0);

    // How many nodes the last question had to compute (the others came from what was kept).
    int processedCount() const { return m_processed; }

private:
    struct Result
    {
        quint64 signature = 0;
        QList<QImage> images; // one for each output socket
    };
    using Done = std::unordered_map<NodeId, Result>;

    Result compute(const Graph &graph, NodeId id, const EvalContext &context, Done &done);
    void keep(const Graph &graph, Done &done); // what was computed stays; nodes that are gone are forgotten

    Done m_kept;
    int m_processed = 0;
};

} // namespace mothion::compositing
