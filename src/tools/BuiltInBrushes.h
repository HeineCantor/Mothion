#pragma once

#include "tools/BrushPreset.h"

#include <QList>

namespace mothion::tools {

// The default sub tools of the brush categories (Pencil, Pen, Brush, Watercolor, Airbrush, Decoration), tuned for the
// engine as it is. Their brushes are made once and shared; their tip images are drawn in code. Empty for a tool that
// is not a brush category.
QList<BrushPreset> brushCategoryPresets(ToolId tool);

} // namespace mothion::tools
