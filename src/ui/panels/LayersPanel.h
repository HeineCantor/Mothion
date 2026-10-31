#pragma once

#include <QWidget>

namespace mothion::ui {

// Placeholder: a static list of fake layers. No data model behind it yet.
class LayersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(QWidget *parent = nullptr);
};

} // namespace mothion::ui
