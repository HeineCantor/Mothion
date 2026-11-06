#pragma once

#include "core/document/Command.h"
#include "core/document/Compositing.h"

namespace mothion::doc {

// Puts `after` in place of the compositing network the document has, and undo puts the old one back. The
// network is small, so a step keeps both whole.
class SetCompositingCommand : public Command
{
public:
    explicit SetCompositingCommand(GraphData after);

    CommandKind kind() const override { return CommandKind::EditCompositing; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    GraphData m_after;
    GraphData m_before;
};

} // namespace mothion::doc
