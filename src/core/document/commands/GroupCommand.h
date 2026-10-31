#pragma once

#include "core/document/Command.h"

#include <memory>
#include <vector>

namespace mothion::doc {

// Several commands undone and redone as one step (e.g. "new drawing" =
// add the drawing + expose it). Applied in order, reverted in reverse.
class GroupCommand : public Command
{
public:
    GroupCommand(CommandKind kind, std::vector<std::unique_ptr<Command>> commands);

    CommandKind kind() const override { return m_kind; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CommandKind m_kind;
    std::vector<std::unique_ptr<Command>> m_commands;
};

} // namespace mothion::doc
