#include "ui/panels/SliderField.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>

namespace mothion::ui {

SliderField::SliderField(const QString &label, int minimum, int maximum, const QString &suffix,
                         QWidget *parent)
    : QWidget(parent)
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_spin(new QSpinBox(this))
{
    auto *caption = new QLabel(label, this);
    caption->setMinimumWidth(64);
    m_slider->setRange(minimum, maximum);
    m_spin->setRange(minimum, maximum);
    m_spin->setSuffix(suffix);
    m_spin->setKeyboardTracking(false);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(caption);
    row->addWidget(m_slider, 1);
    row->addWidget(m_spin);

    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        const QSignalBlocker block(m_spin);
        m_spin->setValue(v);
        emit valueEdited(v);
    });
    connect(m_spin, &QSpinBox::valueChanged, this, [this](int v) {
        const QSignalBlocker block(m_slider);
        m_slider->setValue(v);
        emit valueEdited(v);
    });
}

int SliderField::value() const
{
    return m_spin->value();
}

void SliderField::setValue(int value)
{
    const QSignalBlocker b1(m_slider);
    const QSignalBlocker b2(m_spin);
    m_slider->setValue(value);
    m_spin->setValue(value);
}

} // namespace mothion::ui
