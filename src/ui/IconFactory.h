#pragma once

#include <QIcon>

namespace mothion::ui {

enum class IconId {
    New, Open, Save, Undo, Redo,
    Brush, Eraser, Hand, Zoom,
    Collapse, Expand, Float, Close,
};

// Provisional icons, drawn in code as simple line art on a 24x24 grid.
// Everything that needs an icon asks here, so swapping in real (SVG) artwork
// later only changes this file.
QIcon icon(IconId id);

} // namespace mothion::ui
