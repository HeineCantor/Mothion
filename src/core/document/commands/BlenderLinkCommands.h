#pragma once

#include "core/document/BlenderLink.h"
#include "core/document/Command.h"

#include <optional>

namespace mothion::doc {

// Puts `after` as the Blender link of a cel (none: it is not linked), and undo puts the old one back.
class SetBlenderLinkCommand : public Command
{
public:
    SetBlenderLinkCommand(CelId cel, std::optional<BlenderLink> after);

    CommandKind kind() const override { return CommandKind::EditBlenderLink; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    std::optional<BlenderLink> m_after;
    std::optional<BlenderLink> m_before;
};

} // namespace mothion::doc
