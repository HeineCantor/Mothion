#pragma once

#include "tools/ToolState.h"

#include <QWidget>

class QToolButton;
class QLineEdit;

namespace mothion::ui {

// Simple color picker: current-color swatch (click for the system dialog),
// a hex field and a row of preset swatches.
class ColorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ColorPanel(tools::ToolState *state, QWidget *parent = nullptr);

private:
    void showColor(const QColor &color);

    tools::ToolState *m_state;
    QToolButton *m_current;
    QLineEdit *m_hex;
};

} // namespace mothion::ui
