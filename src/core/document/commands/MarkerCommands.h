#pragma once

#include "core/document/Command.h"

#include <map>
#include <string>

namespace mothion::doc {

// Puts `after` in place of the document's markers (frame -> name), and undo puts the old ones back. Markers are few
// and small, so a step keeps both sets whole.
class SetMarkersCommand : public Command
{
public:
    explicit SetMarkersCommand(std::map<int, std::string> after);

    CommandKind kind() const override { return CommandKind::EditMarkers; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    std::map<int, std::string> m_after;
    std::map<int, std::string> m_before;
};

} // namespace mothion::doc
