#include "core/document/commands/CameraCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetCameraCommand::SetCameraCommand(std::optional<Camera> after)
    : m_after(std::move(after))
{
}

ChangeSet SetCameraCommand::apply(Document &doc)
{
    m_before = doc.camera();
    doc.setCamera(m_after);
    ChangeSet changes;
    changes.camera = true;
    return changes;
}

ChangeSet SetCameraCommand::revert(Document &doc)
{
    doc.setCamera(m_before);
    ChangeSet changes;
    changes.camera = true;
    return changes;
}

std::size_t SetCameraCommand::memoryBytes() const
{
    const std::size_t keys = (m_after ? m_after->keys().size() : 0) + (m_before ? m_before->keys().size() : 0);
    return sizeof(SetCameraCommand) + keys * sizeof(CameraKey);
}

} // namespace mothion::doc
