#pragma once

#include "tools/ToolState.h"

#include <QWidget>

namespace mothion::ui {

// Grid of color swatches; clicking one sets the main color.
// The set is generated (greys + hue rows), placeholder until user palettes exist.
class ColorSetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorSetPanel(tools::ToolState *state, QWidget *parent = nullptr);
};

} // namespace mothion::ui
