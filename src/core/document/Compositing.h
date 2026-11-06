#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mothion::doc {

// The compositing network as the document keeps it: plain data, which the project file stores and undo swaps.
// What the nodes mean (their types, sockets and settings) lives in the compositing module; here a node is a type
// name, a place in the editor and its settings as integers (a color is its packed ARGB value).
struct GraphNodeData
{
    int id = 0;
    std::string type;
    double x = 0;
    double y = 0;
    std::vector<std::pair<std::string, int>> params;
    std::string data{}; // what does not fit in numbers (the areas of a mask), as the node's type writes it

    bool operator==(const GraphNodeData &) const = default;
};

struct GraphLinkData
{
    int from = 0;
    int fromPort = 0;
    int to = 0;
    int toPort = 0;

    bool operator==(const GraphLinkData &) const = default;
};

struct GraphData
{
    std::vector<GraphNodeData> nodes; // none: the project has no network yet (the editor starts one)
    std::vector<GraphLinkData> links;
    int nextId = 1;

    bool operator==(const GraphData &) const = default;
};

// How the editor was left: the zoom of the node view and the scene point at its center. Not part of the network
// (moving the view is no undo step); the project keeps it so the editor opens as it was.
struct GraphView
{
    bool valid = false;
    double zoom = 1;
    double x = 0;
    double y = 0;

    bool operator==(const GraphView &) const = default;
};

} // namespace mothion::doc
