#include "ui/panels/ColorWheelPanel.h"

#include "ui/panels/ColorWheel.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>

namespace mothion::ui {

// --- ColorSwatchPair ----------------------------------------------------------

ColorSwatchPair::ColorSwatchPair(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
    setToolTip(tr("Main and secondary color: click one to select it (X switches), click the selected one to "
                  "choose its color; the bar below is the transparent color: the brush erases with it"));
}

void ColorSwatchPair::setColors(const QColor &main, const QColor &secondary, bool secondarySelected)
{
    m_main = main;
    m_secondary = secondary;
    m_secondarySelected = secondarySelected;
    update();
}

void ColorSwatchPair::setTransparentSelected(bool on)
{
    m_transparentSelected = on;
    update();
}

QRectF ColorSwatchPair::mainRect() const
{
    return {1, 1, kSquares * 0.62, kSquares * 0.62};
}

QRectF ColorSwatchPair::secondaryRect() const
{
    return {kSquares * 0.37, kSquares * 0.37, kSquares * 0.62, kSquares * 0.62};
}

QRectF ColorSwatchPair::transparentRect() const
{
    return {1.0, double(kSquares + kBarGap), width() - 2.0, double(kBarHeight) - 1.0};
}

void ColorSwatchPair::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor border = palette().dark().color();
    // The places stay: main in front, secondary behind. The selected one is outlined in the highlight color.
    const QColor selected = palette().highlight().color();
    const bool mainSelected = !m_transparentSelected && !m_secondarySelected;
    const bool secondarySelected = !m_transparentSelected && m_secondarySelected;
    p.setPen(QPen(border, 1));
    p.setBrush(m_secondary);
    p.drawRoundedRect(secondaryRect(), 3, 3);
    p.setPen(QPen(mainSelected ? selected : border, mainSelected ? 2.0 : 1.0));
    p.setBrush(m_main);
    p.drawRoundedRect(mainRect(), 3, 3);
    if (secondarySelected) { // its outline, what the main square leaves of it
        QPainterPath outside;
        outside.addRect(rect());
        QPainterPath front;
        front.addRoundedRect(mainRect().adjusted(-1, -1, 1, 1), 3, 3);
        p.save();
        p.setClipPath(outside.subtracted(front));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(selected, 2.0));
        p.drawRoundedRect(secondaryRect(), 3, 3);
        p.restore();
    }

    // The transparent color: a checkerboard in a rounded bar.
    const QRectF bar = transparentRect();
    QPainterPath shape;
    shape.addRoundedRect(bar, 3, 3);
    p.save();
    p.setClipPath(shape);
    constexpr int cell = 5;
    for (int y = 0; y * cell < bar.height(); ++y)
        for (int x = 0; x * cell < bar.width(); ++x)
            p.fillRect(QRectF(bar.left() + x * cell, bar.top() + y * cell, cell, cell),
                       (x + y) % 2 ? QColor(0xcc, 0xcc, 0xcc) : QColor(Qt::white));
    p.restore();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(m_transparentSelected ? palette().highlight().color() : border, m_transparentSelected ? 2.0 : 1.0));
    p.drawRoundedRect(bar, 3, 3);
}

void ColorSwatchPair::mousePressEvent(QMouseEvent *event)
{
    if (transparentRect().contains(event->position()))
        emit transparentClicked();
    else if (mainRect().contains(event->position()))
        emit mainClicked();
    else if (secondaryRect().contains(event->position()))
        emit secondaryClicked();
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
    connect(m_swatches, &ColorSwatchPair::secondaryClicked, this, [this] {
        // A click selects; on the one already selected it opens the color dialog.
        if (m_state->transparentSelected() || !m_state->secondarySelected()) {
            m_state->selectSecondary(true);
            return;
        }
        m_state->setColor(QColorDialog::getColor(m_state->color(), this, tr("Choose color")));
    });
    connect(m_swatches, &ColorSwatchPair::transparentClicked, this,
            [this] { m_state->setTransparentSelected(true); });
    connect(m_state, &tools::ToolState::transparentSelectedChanged, m_swatches,
            &ColorSwatchPair::setTransparentSelected);
    connect(m_swatches, &ColorSwatchPair::mainClicked, this, [this] {
        if (m_state->transparentSelected() || m_state->secondarySelected()) { // back to the main color
            m_state->selectSecondary(false);
            return;
        }
        const QColor c = QColorDialog::getColor(m_state->color(), this, tr("Choose color"));
        m_state->setColor(c); // an invalid (cancelled) color is ignored by ToolState
    });
    // A field that loses focus as its panel is being destroyed still finishes editing: what it needs is taken along,
    // not read from the panel (which is half gone by then).
    connect(m_hex, &QLineEdit::editingFinished, this,
            [state = m_state, hex = m_hex] { state->setColor(QColor(hex->text())); });
    connect(m_state, &tools::ToolState::colorChanged, this, &ColorWheelPanel::showColor);
    connect(m_state, &tools::ToolState::secondaryColorChanged, this, [this] { showColor(m_state->color()); });
    connect(m_state, &tools::ToolState::selectedColorChanged, this, [this] { showColor(m_state->color()); });
    showColor(m_state->color());
}

void ColorWheelPanel::showColor(const QColor &color)
{
    if (color != m_wheel->color()) // don't fight the wheel while it is being dragged
        m_wheel->setColor(color);
    m_swatches->setColors(m_state->mainColor(), m_state->secondaryColor(), m_state->secondarySelected());
    const QColor hsv = color.toHsv();
    m_hsv->setText(QStringLiteral("H %1  S %2  V %3")
                       .arg(std::max(0, hsv.hsvHue()))
                       .arg(qRound(hsv.hsvSaturationF() * 100))
                       .arg(qRound(hsv.valueF() * 100)));
    m_hex->setText(color.name());
}

} // namespace mothion::ui
