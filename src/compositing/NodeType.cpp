#include "compositing/NodeType.h"

#include "compositing/nodes/BlurNode.h"
#include "compositing/nodes/ChromaticAberrationNode.h"
#include "compositing/nodes/ColorCorrectNode.h"
#include "compositing/nodes/ColorNode.h"
#include "compositing/nodes/DirectionalBlurNode.h"
#include "compositing/nodes/DropShadowNode.h"
#include "compositing/nodes/LevelsNode.h"
#include "compositing/nodes/FrameNode.h"
#include "compositing/nodes/GlowNode.h"
#include "compositing/nodes/MaskNode.h"
#include "compositing/nodes/MergeNode.h"
#include "compositing/nodes/OpacityNode.h"
#include "compositing/nodes/OutputNode.h"
#include "compositing/nodes/SensorNoiseNode.h"
#include "compositing/nodes/SharpenNode.h"
#include "compositing/nodes/TransformNode.h"
#include "compositing/nodes/VignetteNode.h"

namespace mothion::compositing {

NodeRegistry::NodeRegistry()
{
    // The order is the one the "Add node" menu shows within each category.
    m_types.push_back(std::make_unique<FrameNode>());
    m_types.push_back(std::make_unique<OutputNode>());
    m_types.push_back(std::make_unique<ColorCorrectNode>());
    m_types.push_back(std::make_unique<LevelsNode>());
    m_types.push_back(std::make_unique<ColorNode>());
    m_types.push_back(std::make_unique<BlurNode>());
    m_types.push_back(std::make_unique<DirectionalBlurNode>());
    m_types.push_back(std::make_unique<SharpenNode>());
    m_types.push_back(std::make_unique<GlowNode>());
    m_types.push_back(std::make_unique<ChromaticAberrationNode>());
    m_types.push_back(std::make_unique<SensorNoiseNode>());
    m_types.push_back(std::make_unique<VignetteNode>());
    m_types.push_back(std::make_unique<DropShadowNode>());
    m_types.push_back(std::make_unique<TransformNode>());
    m_types.push_back(std::make_unique<MergeNode>());
    m_types.push_back(std::make_unique<OpacityNode>());
    m_types.push_back(std::make_unique<MaskNode>());
}

QString categoryTitle(NodeCategory category)
{
    switch (category) {
    case NodeCategory::InputOutput: return nodeTr("Input / Output");
    case NodeCategory::Color: return nodeTr("Color");
    case NodeCategory::Filter: return nodeTr("Filter");
    case NodeCategory::Effect: return nodeTr("Effect");
    case NodeCategory::Transform: return nodeTr("Transform");
    case NodeCategory::Composite: return nodeTr("Composite");
    }
    return {};
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
