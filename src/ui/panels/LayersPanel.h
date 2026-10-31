#pragma once

#include <QWidget>

namespace mothion::ui {

// Placeholder layer stack: blend mode and opacity of the selected layer on
// top, then one row per layer (visibility, thumbnail, opacity/mode, name).
// No data model behind it yet; the controls do nothing.
class LayersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(QWidget *parent = nullptr);
};

} // namespace mothion::ui
