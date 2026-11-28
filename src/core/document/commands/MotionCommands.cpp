#include "core/document/commands/MotionCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetCelMotionCommand::SetCelMotionCommand(CelId cel, CelMotion after)
    : m_cel(cel)
    , m_after(std::move(after))
{
}

SetCelMotionCommand::SetCelMotionCommand(CelId cel, CelMotion before, CelMotion after)
    : m_cel(cel)
    , m_after(std::move(after))
    , m_before(std::move(before))
{
}

ChangeSet SetCelMotionCommand::apply(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        m_before = cel->motion();
        cel->setMotion(m_after);
        changes.motion = true;
    }
    return changes;
}

ChangeSet SetCelMotionCommand::revert(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        cel->setMotion(m_before);
        changes.motion = true;
    }
    return changes;
}

std::size_t SetCelMotionCommand::memoryBytes() const
{
    std::size_t keys = 0;
    for (Channel c : kChannels)
        keys += m_after.keys(c).size() + m_before.keys(c).size();
    return sizeof(SetCelMotionCommand) + keys * sizeof(CelKey);
}

SetDeformerCommand::SetDeformerCommand(CelId cel, Deformer after)
    : m_cel(cel)
    , m_after(std::move(after))
{
}

ChangeSet SetDeformerCommand::apply(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        m_before = cel->deformer();
        cel->setDeformer(m_after);
        changes.motion = true; // what it bends is drawn again, as for a key
    }
    return changes;
}

ChangeSet SetDeformerCommand::revert(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        cel->setDeformer(m_before);
        changes.motion = true;
    }
    return changes;
}

std::size_t SetDeformerCommand::memoryBytes() const
{
    return sizeof(SetDeformerCommand) + (m_after.bones.size() + m_before.bones.size()) * sizeof(Bone)
           + (m_after.rest.size() + m_before.rest.size()) * sizeof(double);
}

SetModelRigCommand::SetModelRigCommand(CelId cel, ModelRig after)
    : m_cel(cel)
    , m_after(std::move(after))
{
}

ChangeSet SetModelRigCommand::apply(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        m_before = cel->model();
        cel->setModel(m_after);
        changes.motion = true; // drawn again, as for a key
    }
    return changes;
}

ChangeSet SetModelRigCommand::revert(Document &doc)
{
    ChangeSet changes;
    if (Cel *cel = doc.cel(m_cel)) {
        cel->setModel(m_before);
        changes.motion = true;
    }
    return changes;
}

std::size_t SetModelRigCommand::memoryBytes() const
{
    // The model itself is shared with the cel: only the settings are this step's own.
    return sizeof(SetModelRigCommand) + (m_after.joints.size() + m_before.joints.size()) * sizeof(rig3d::JointSettings);
}

} // namespace mothion::doc
