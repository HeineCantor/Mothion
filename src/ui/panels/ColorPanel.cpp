#include "ui/panels/ColorPanel.h"

#include <QColorDialog>
#include <QGridLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

QString swatchStyle(const QColor &c)
{
    return QStringLiteral("QToolButton { background: %1; border: 1px solid palette(dark); }")
        .arg(c.name());
}

} // namespace

ColorPanel::ColorPanel(tools::ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_current(new QToolButton(this))
    , m_hex(new QLineEdit(this))
{
    m_current->setFixedSize(56, 40);
    m_current->setToolTip(tr("Colore corrente (clic per scegliere)"));
    m_hex->setInputMask(QStringLiteral("\\#HHHHHH"));
    m_hex->setMaximumWidth(90);

    auto *top = new QHBoxLayout;
    top->addWidget(m_current);
    top->addWidget(m_hex);
    top->addStretch(1);

    // Neutral presets plus a few hues: placeholder until real palettes exist.
    const char *presets[] = {"#000000", "#20202a", "#5a5a64", "#a0a0a8", "#ffffff", "#c83232",
                             "#e07828", "#e8c040", "#4caf50", "#2e8b8b", "#3a6ed0", "#7a4ac8",
                             "#d04a90", "#8b5a3c", "#f0c8a0", "#9ec8f0"};
    auto *grid = new QGridLayout;
    grid->setSpacing(2);
    int i = 0;
    for (const char *hex : presets) {
        const QColor c(QString::fromLatin1(hex));
        auto *swatch = new QToolButton(this);
        swatch->setFixedSize(20, 20);
        swatch->setStyleSheet(swatchStyle(c));
        swatch->setToolTip(c.name());
        connect(swatch, &QToolButton::clicked, m_state, [this, c] { m_state->setColor(c); });
        grid->addWidget(swatch, i / 8, i % 8);
        ++i;
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addLayout(top);
    layout->addLayout(grid);
    layout->addStretch(1);

    connect(m_current, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_state->color(), this, tr("Scegli colore"));
        m_state->setColor(c); // invalid (cancelled) colors are ignored by ToolState
    });
    connect(m_hex, &QLineEdit::editingFinished, this,
            [this] { m_state->setColor(QColor(m_hex->text())); });
    connect(m_state, &tools::ToolState::colorChanged, this, &ColorPanel::showColor);
    showColor(m_state->color());
}

void ColorPanel::showColor(const QColor &color)
{
    m_current->setStyleSheet(swatchStyle(color));
    m_hex->setText(color.name());
}

} // namespace mothion::ui
