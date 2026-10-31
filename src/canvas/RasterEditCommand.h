#pragma once

#include "canvas/RasterSurface.h"

#include <QUndoCommand>

#include <functional>

namespace mothion::canvas {

// Undo/redo entry for one finished stroke: swaps the stroke's before/after
// pixels back into the surface.
//
// Provisional: it lives on the canvas's raster because there is no document
// model yet. When layers and frames exist, commands will target the model.
class RasterEditCommand : public QUndoCommand
{
public:
    // `apply` writes a patch into the surface and repaints; the command knows
    // nothing else about the canvas.
    using ApplyPatch = std::function<void(const RasterPatch &)>;

    RasterEditCommand(const QString &text, RasterEdit edit, ApplyPatch apply);

    void undo() override;
    void redo() override;

private:
    RasterEdit m_edit;
    ApplyPatch m_apply;
    bool m_firstRedo = true; // QUndoStack::push() calls redo(); the stroke is already on screen
};

} // namespace mothion::canvas
