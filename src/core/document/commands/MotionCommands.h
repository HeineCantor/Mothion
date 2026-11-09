#pragma once

#include "core/document/CelMotion.h"
#include "core/document/Command.h"

namespace mothion::doc {

// Puts `after` in place of the motion of a cel (its pivot and keys), and undo puts the old one back. A motion is small, so
// a step keeps both whole.
class SetCelMotionCommand : public Command
{
public:
    SetCelMotionCommand(CelId cel, CelMotion after);
    // For a change already made in the document (a drag, seen as it went): `before` is what it was.
    SetCelMotionCommand(CelId cel, CelMotion before, CelMotion after);

    CommandKind kind() const override { return CommandKind::EditMotion; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    CelMotion m_after;
    CelMotion m_before;
};

} // namespace mothion::doc
