#pragma once

#include "core/document/Command.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>

namespace mothion::doc {

struct HistoryLimits
{
    std::size_t maxSteps = 500;
    std::size_t maxBytes = std::size_t(1) << 30; // 1 GiB of undo data
};

// Undo/redo stack of Commands.
//
// The oldest steps are dropped when either limit is exceeded; the most
// recent step is always kept, even if it alone is over the memory limit.
// A "clean" mark records the state that matches the saved file.
class History
{
public:
    explicit History(HistoryLimits limits = {});
    // New limits; steps over them go at once (the oldest first).
    void setLimits(HistoryLimits limits);

    // Applies the command and records it (a nullptr command does nothing).
    ChangeSet execute(Document &doc, std::unique_ptr<Command> command);
    // Records a command whose effect is already in the document (e.g. a
    // stroke painted live, captured afterwards).
    void pushApplied(std::unique_ptr<Command> command);

    bool canUndo() const { return m_applied > 0; }
    bool canRedo() const { return m_applied < m_commands.size(); }
    ChangeSet undo(Document &doc);
    ChangeSet redo(Document &doc);

    const Command *undoCommand() const; // next to undo, or nullptr
    const Command *redoCommand() const;

    std::size_t size() const { return m_commands.size(); }
    std::size_t appliedCount() const { return m_applied; }
    std::size_t memoryBytes() const { return m_bytes; }

    void markClean() { m_cleanIndex = m_applied; }
    bool isClean() const { return m_cleanIndex == m_applied; }
    // The state matches no saved file (a project recovered from an autosave is not what its file holds).
    void markUnsaved() { m_cleanIndex.reset(); }

    void clear();

private:
    void push(std::unique_ptr<Command> command);
    void enforceLimits();
    void recountBytes();

    HistoryLimits m_limits;
    std::deque<std::unique_ptr<Command>> m_commands;
    std::size_t m_applied = 0; // commands [0, m_applied) are in effect
    std::size_t m_bytes = 0;
    std::optional<std::size_t> m_cleanIndex = 0; // nullopt: clean state no longer reachable
};

} // namespace mothion::doc
