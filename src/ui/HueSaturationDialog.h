#pragma once

#include <QDialog>

#include <vector>

class QSpinBox;

namespace mothion::ui {

// Edit > Color > Hue/Saturation/Value: three sliders (each with its number), the hue -180..180 degrees, the saturation
// and the value -100..100. Under each, a strip of colors shows what it does: under the hue the wheel as it is and as
// it becomes; under the saturation and the value, a color as each place of the slider would make it (with the other
// two as they are set). Every change is sent (changed()) for the canvas to show it at once; OK keeps it, Cancel drops
// it (the caller applies or drops: see DocumentController::beginColorAdjust()). It does not block the window: the
// canvas behind is seen as it is, not dimmed.
class HueSaturationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HueSaturationDialog(QWidget *parent = nullptr);

    int hue() const;
    int saturation() const;
    int value() const;
    void setValues(int hue, int saturation, int value);

signals:
    void changed(int hue, int saturation, int value);

private:
    QSpinBox *m_hue = nullptr;
    QSpinBox *m_saturation = nullptr;
    QSpinBox *m_value = nullptr;
    std::vector<QWidget *> m_strips;
    bool m_quiet = false;
};

} // namespace mothion::ui
