#pragma once

#include <QWidget>

class QSlider;
class QSpinBox;

namespace mothion::ui {

// "Label  [slider]  [spin]" row with the two editors kept in sync.
// Small helper used by the property panels.
class SliderField : public QWidget
{
    Q_OBJECT

public:
    SliderField(const QString &label, int minimum, int maximum, const QString &suffix,
                QWidget *parent = nullptr);

    int value() const;
    void setValue(int value); // does not emit valueEdited

signals:
    void valueEdited(int value); // only on user edits

private:
    QSlider *m_slider;
    QSpinBox *m_spin;
};

} // namespace mothion::ui
