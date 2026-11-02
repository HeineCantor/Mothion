#pragma once

#include <QIcon>

namespace mothion::ui {

enum class IconId {
    New, Open, Save, Undo, Redo,
    Brush, Eraser, Hand, Zoom, Move, RectSelect, LassoSelect, Puppet, Eyedropper, Fill,
    Collapse, Expand, Float, Close,
    ZoomOut, ZoomFit, ZoomActual, RotateLeft, RotateRight, RotateReset,
    FlipHorizontal, FlipVertical, Eye, Lock, Add, Delete, ArrowUp, ArrowDown, Clear,
    Deselect, InvertSelection, EraseSelection, CutToLayer, CopyToLayer,
};

// Provisional icons, drawn in code as simple line art on a 24x24 grid.
// Everything that needs an icon asks here, so swapping in real (SVG) artwork
// later only changes this file.
QIcon icon(IconId id);

} // namespace mothion::ui
