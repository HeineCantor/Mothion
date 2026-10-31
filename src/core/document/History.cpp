#include "core/document/History.h"

namespace mothion::doc {

History::History(HistoryLimits limits)
    : m_limits(limits)
{
}

ChangeSet History::execute(Document &doc, std::unique_ptr<Command> command)
{
    if (!command)
        return {};
    ChangeSet changes = command->apply(doc);
    push(std::move(command));
    return changes;
}

void History::pushApplied(std::unique_ptr<Command> command)
{
    if (command)
        push(std::move(command));
}

void History::push(std::unique_ptr<Command> command)
{
    // A new step discards the redo branch.
    if (m_cleanIndex && *m_cleanIndex > m_applied)
        m_cleanIndex.reset();
    m_commands.erase(m_commands.begin() + std::ptrdiff_t(m_applied), m_commands.end());
    m_commands.push_back(std::move(command));
    m_applied = m_commands.size();
    // Memory can change after apply/revert (commands hold what is not in
    // the document), so it is recounted rather than tracked incrementally.
    recountBytes();
    enforceLimits();
}

ChangeSet History::undo(Document &doc)
{
    if (!canUndo())
        return {};
    ChangeSet changes = m_commands[--m_applied]->revert(doc);
    recountBytes();
    return changes;
}

ChangeSet History::redo(Document &doc)
{
    if (!canRedo())
        return {};
    ChangeSet changes = m_commands[m_applied++]->apply(doc);
    recountBytes();
    return changes;
}

const Command *History::undoCommand() const
{
    return canUndo() ? m_commands[m_applied - 1].get() : nullptr;
}

const Command *History::redoCommand() const
{
    return canRedo() ? m_commands[m_applied].get() : nullptr;
}

void History::clear()
{
    // The document itself does not change: if it matched the file, it still does.
    m_cleanIndex = isClean() ? std::optional<std::size_t>(0) : std::nullopt;
    m_commands.clear();
    m_applied = 0;
    m_bytes = 0;
}

void History::recountBytes()
{
    m_bytes = 0;
    for (const auto &c : m_commands)
        m_bytes += c->memoryBytes();
}

void History::enforceLimits()
{
    while (m_applied > 1 && (m_commands.size() > m_limits.maxSteps || m_bytes > m_limits.maxBytes)) {
        m_bytes -= m_commands.front()->memoryBytes();
        m_commands.pop_front();
        --m_applied;
        if (m_cleanIndex)
            m_cleanIndex = *m_cleanIndex > 0 ? std::optional<std::size_t>(*m_cleanIndex - 1) : std::nullopt;
    }
}

} // namespace mothion::doc
