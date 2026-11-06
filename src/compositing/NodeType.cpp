#include "compositing/NodeType.h"

#include "compositing/nodes/BlurNode.h"
#include "compositing/nodes/ChromaticAberrationNode.h"
#include "compositing/nodes/ColorNode.h"
#include "compositing/nodes/FrameNode.h"
#include "compositing/nodes/MergeNode.h"
#include "compositing/nodes/OutputNode.h"
#include "compositing/nodes/SensorNoiseNode.h"

namespace mothion::compositing {

NodeRegistry::NodeRegistry()
{
    // The order is the one the "Add node" menu shows.
    m_types.push_back(std::make_unique<FrameNode>());
    m_types.push_back(std::make_unique<BlurNode>());
    m_types.push_back(std::make_unique<ChromaticAberrationNode>());
    m_types.push_back(std::make_unique<SensorNoiseNode>());
    m_types.push_back(std::make_unique<MergeNode>());
    m_types.push_back(std::make_unique<ColorNode>());
    m_types.push_back(std::make_unique<OutputNode>());
}

const NodeRegistry &NodeRegistry::instance()
{
    static const NodeRegistry registry;
    return registry;
}

const NodeType *NodeRegistry::find(const QString &id) const
{
    for (const auto &type : m_types)
        if (type->id() == id)
            return type.get();
    return nullptr;
}

} // namespace mothion::compositing
