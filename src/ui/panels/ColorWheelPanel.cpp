#include "ui/panels/ColorWheelPanel.h"

#include "ui/panels/ColorWheel.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

namespace mothion::ui {

// --- ColorSwatchPair ----------------------------------------------------------

ColorSwatchPair::ColorSwatchPair(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
    setToolTip(tr("Colore principale / secondario (clic sul secondario per scambiarli)"));
}

void ColorSwatchPair::setColors(const QColor &main, const QColor &secondary)
{
    m_main = main;
    m_secondary = secondary;
    update();
}

QRectF ColorSwatchPair::mainRect() const
{
    return {1, 1, width() * 0.62, height() * 0.62};
}

QRectF ColorSwatchPair::secondaryRect() const
{
    return {width() * 0.37, height() * 0.37, width() * 0.62, height() * 0.62};
}

void ColorSwatchPair::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor border = palette().dark().color();
    p.setPen(QPen(border, 1));
    p.setBrush(m_secondary);
    p.drawRoundedRect(secondaryRect(), 3, 3);
    p.setPen(QPen(palette().light().color(), 1.5));
    p.setBrush(m_main);
    p.drawRoundedRect(mainRect(), 3, 3);
}

void ColorSwatchPair::mousePressEvent(QMouseEvent *event)
{
    if (mainRect().contains(event->position()))
        emit mainClicked();
    else if (secondaryRect().contains(event->position()))
        emit swapRequested();
}

// --- ColorWheelPanel ----------------------------------------------------------

ColorWheelPanel::ColorWheelPanel(tools::ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_wheel(new ColorWheel(this))
    , m_swatches(new ColorSwatchPair(this))
    , m_hsv(new QLabel(this))
    , m_hex(new QLineEdit(this))
{
    m_hex->setInputMask(QStringLiteral("\\#HHHHHH"));
    m_hex->setMaximumWidth(80);
    m_hsv->setEnabled(false);

    auto *readout = new QHBoxLayout;
    readout->addWidget(m_swatches, 0, Qt::AlignBottom);
    readout->addStretch(1);
    auto *numbers = new QVBoxLayout;
    numbers->addWidget(m_hsv, 0, Qt::AlignRight);
    numbers->addWidget(m_hex, 0, Qt::AlignRight);
    readout->addLayout(numbers);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(m_wheel, 1);
    layout->addLayout(readout);

    connect(m_wheel, &ColorWheel::colorEdited, m_state, &tools::ToolState::setColor);
    connect(m_swatches, &ColorSwatchPair::swapRequested, m_state, &tools::ToolState::swapColors);
    connect(m_swatches, &ColorSwatchPair::mainClicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_state->color(), this, tr("Scegli colore"));
        m_state->setColor(c); // an invalid (cancelled) color is ignored by ToolState
    });
    connect(m_hex, &QLineEdit::editingFinished, this, [this] { m_state->setColor(QColor(m_hex->text())); });
    connect(m_state, &tools::ToolState::colorChanged, this, &ColorWheelPanel::showColor);
    connect(m_state, &tools::ToolState::secondaryColorChanged, this,
            [this] { m_swatches->setColors(m_state->color(), m_state->secondaryColor()); });
    showColor(m_state->color());
}

void ColorWheelPanel::showColor(const QColor &color)
{
    if (color != m_wheel->color()) // don't fight the wheel while it is being dragged
        m_wheel->setColor(color);
    m_swatches->setColors(color, m_state->secondaryColor());
    const QColor hsv = color.toHsv();
    m_hsv->setText(QStringLiteral("H %1  S %2  V %3")
                       .arg(std::max(0, hsv.hsvHue()))
                       .arg(qRound(hsv.hsvSaturationF() * 100))
                       .arg(qRound(hsv.valueF() * 100)));
    m_hex->setText(color.name());
}

} // namespace mothion::ui
