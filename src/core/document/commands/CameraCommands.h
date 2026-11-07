#pragma once

#include "core/document/Camera.h"
#include "core/document/Command.h"

#include <optional>

namespace mothion::doc {

// Puts `after` in place of the camera the document has (none, or a camera with its keys), and undo puts the old
// one back. A camera is small, so a step keeps both whole.
class SetCameraCommand : public Command
{
public:
    explicit SetCameraCommand(std::optional<Camera> after);

    CommandKind kind() const override { return CommandKind::EditCamera; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    std::optional<Camera> m_after;
    std::optional<Camera> m_before;
};

} // namespace mothion::doc
