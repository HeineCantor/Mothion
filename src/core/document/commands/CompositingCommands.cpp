#include "core/document/commands/CompositingCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetCompositingCommand::SetCompositingCommand(GraphData after)
    : m_after(std::move(after))
{
}

ChangeSet SetCompositingCommand::apply(Document &doc)
{
    m_before = doc.compositing();
    doc.setCompositing(m_after);
    ChangeSet changes;
    changes.structure = true;
    return changes;
}

ChangeSet SetCompositingCommand::revert(Document &doc)
{
    doc.setCompositing(m_before);
    ChangeSet changes;
    changes.structure = true;
    return changes;
}

std::size_t SetCompositingCommand::memoryBytes() const
{
    return 2 * (sizeof(GraphData) + 256 * (m_after.nodes.size() + m_before.nodes.size() + 1));
}

} // namespace mothion::doc
