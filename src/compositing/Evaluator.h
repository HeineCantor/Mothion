#pragma once

#include "compositing/Graph.h"
#include "compositing/NodeType.h"

#include <unordered_map>

namespace mothion::compositing {

// Runs a Graph: from the Output node back along the wires, each node processes what its inputs give.
// What a node produced is kept, and used again while nothing it depends on has changed (its settings,
// its wires, the nodes upstream, and for those that look at it, the frame): editing one node only
// computes it and what comes after it.
class Evaluator
{
public:
    // The image that reaches the Output node: null if the graph has no Output or nothing is wired to it.
    QImage evaluate(const Graph &graph, const EvalContext &context);

    // How many nodes the last evaluate() had to compute (the others came from what was kept).
    int processedCount() const { return m_processed; }

private:
    struct Result
    {
        quint64 signature = 0;
        QImage image;
    };

    Result compute(const Graph &graph, NodeId id, const EvalContext &context, std::unordered_map<NodeId, Result> &done);

    std::unordered_map<NodeId, Result> m_kept;
    int m_processed = 0;
};

} // namespace mothion::compositing
