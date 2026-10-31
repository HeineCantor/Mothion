#pragma once

#include "tools/ToolState.h"

#include <QWidget>

namespace mothion::ui {

class VerticalValueSlider;

// Two tall sliders beside the canvas for the active tool: size (top) and
// opacity (bottom), always at hand while drawing.
class BrushSlidersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit BrushSlidersPanel(tools::ToolState *state, QWidget *parent = nullptr);

private:
    void syncFromState();

    tools::ToolState *m_state;
    VerticalValueSlider *m_size;
    VerticalValueSlider *m_opacity;
};

} // namespace mothion::ui
