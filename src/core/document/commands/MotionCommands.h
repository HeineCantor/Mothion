#pragma once

#include "core/document/CelMotion.h"
#include "core/document/Command.h"
#include "core/document/Deformer.h"

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
    void addChangedCels(std::set<CelId> &cels) const override { cels.insert(m_cel); }
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    CelMotion m_after;
    CelMotion m_before;
};

// Puts `after` in place of a deformation cel's bones and rest (see Deformer), and undo puts the old ones back.
class SetDeformerCommand : public Command
{
public:
    SetDeformerCommand(CelId cel, Deformer after);

    CommandKind kind() const override { return CommandKind::EditDeformer; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    void addChangedCels(std::set<CelId> &cels) const override { cels.insert(m_cel); }
    std::size_t memoryBytes() const override;

private:
    CelId m_cel;
    Deformer m_after;
    Deformer m_before;
};

} // namespace mothion::doc
