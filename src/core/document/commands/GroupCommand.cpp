#include "core/document/commands/GroupCommand.h"

namespace mothion::doc {

GroupCommand::GroupCommand(CommandKind kind, std::vector<std::unique_ptr<Command>> commands)
    : m_kind(kind)
    , m_commands(std::move(commands))
{
}

ChangeSet GroupCommand::apply(Document &doc)
{
    ChangeSet changes;
    for (auto &c : m_commands)
        changes.merge(c->apply(doc));
    return changes;
}

ChangeSet GroupCommand::revert(Document &doc)
{
    ChangeSet changes;
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
        changes.merge((*it)->revert(doc));
    return changes;
}

std::size_t GroupCommand::memoryBytes() const
{
    std::size_t total = 0;
    for (const auto &c : m_commands)
        total += c->memoryBytes();
    return total;
}

} // namespace mothion::doc
