#include "canvas/RasterEditCommand.h"

namespace mothion::canvas {

RasterEditCommand::RasterEditCommand(const QString &text, RasterEdit edit, ApplyPatch apply)
    : QUndoCommand(text)
    , m_edit(std::move(edit))
    , m_apply(std::move(apply))
{
}

void RasterEditCommand::undo()
{
    m_apply(m_edit.before);
}

void RasterEditCommand::redo()
{
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_apply(m_edit.after);
}

} // namespace mothion::canvas
