#include "ui/panels/ColorSetPanel.h"

#include <QGridLayout>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

constexpr int kColumns = 8;

QList<QColor> generatedSet()
{
    QList<QColor> colors;
    for (int i = 0; i < kColumns; ++i) // grey ramp, black to white
        colors << QColor::fromHsvF(0, 0, float(i) / (kColumns - 1));
    // Rows of hues at different saturation / value, so skin, shadow and
    // pastel tones are all one click away.
    const float rows[][2] = {{0.9f, 0.9f}, {0.55f, 1.0f}, {0.25f, 1.0f}, {0.8f, 0.55f}, {0.45f, 0.75f}};
    for (const auto &sv : rows)
        for (int i = 0; i < kColumns; ++i)
            colors << QColor::fromHsvF(float(i) / kColumns, sv[0], sv[1]);
    return colors;
}

} // namespace

ColorSetPanel::ColorSetPanel(tools::ToolState *state, QWidget *parent)
    : QWidget(parent)
{
    auto *grid = new QGridLayout;
    grid->setSpacing(1);
    grid->setContentsMargins(0, 0, 0, 0);
    const QList<QColor> colors = generatedSet();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor c = colors[i];
        auto *swatch = new QToolButton(this);
        swatch->setMinimumSize(18, 18);
        swatch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        swatch->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: none; }"
                                             "QToolButton:hover { border: 1px solid palette(highlight); }")
                                  .arg(c.name()));
        swatch->setToolTip(c.name());
        connect(swatch, &QToolButton::clicked, state, [state, c] { state->setColor(c); });
        grid->addWidget(swatch, i / kColumns, i % kColumns);
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(grid);
    layout->addStretch(1);
}

} // namespace mothion::ui
