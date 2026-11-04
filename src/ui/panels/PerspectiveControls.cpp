#include "ui/panels/PerspectiveControls.h"

#include "core/document/commands/LayerCommands.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

#include <cmath>

namespace mothion::ui {

using document::DocumentController;

namespace {

QIcon swatch(const QColor &color)
{
    QPixmap pm(28, 14);
    pm.fill(color);
    QPainter p(&pm);
    p.setPen(QColor(0, 0, 0, 120));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return QIcon(pm);
}

} // namespace

PerspectiveControls::PerspectiveControls(DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_color(new QToolButton(this))
    , m_horizon(new QCheckBox(tr("Horizon"), this))
{
    setObjectName(QStringLiteral("perspectiveControls"));
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 2, 0, 2);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    grid->setColumnStretch(1, 1);

    auto round = [](double v) { return int(std::lround(v)); };
    addField(tr("Turn"), -180, 180, QStringLiteral("°"), QStringLiteral("perspectiveTurn"),
             {[round](const perspective::Grid &g) { return round(g.turn); },
              [](perspective::Grid &g, int v) { g.turn = v; }});
    addField(tr("Tilt"), 0, 89, QStringLiteral("°"), QStringLiteral("perspectiveTilt"),
             {[round](const perspective::Grid &g) { return round(g.tilt); },
              [](perspective::Grid &g, int v) { g.tilt = v; }});
    addField(tr("Roll"), -180, 180, QStringLiteral("°"), QStringLiteral("perspectiveRoll"),
             {[round](const perspective::Grid &g) { return round(g.roll); },
              [](perspective::Grid &g, int v) { g.roll = v; }});
    addField(tr("Lens"), 10, 150, QStringLiteral("°"), QStringLiteral("perspectiveFov"),
             {[round](const perspective::Grid &g) { return round(g.fov); },
              [](perspective::Grid &g, int v) { g.fov = v; }});
    addField(tr("Center X"), -20000, 20000, QStringLiteral(" px"), QStringLiteral("perspectiveCenterX"),
             {[round](const perspective::Grid &g) { return round(g.centerX); },
              [](perspective::Grid &g, int v) { g.centerX = v; }});
    addField(tr("Center Y"), -20000, 20000, QStringLiteral(" px"), QStringLiteral("perspectiveCenterY"),
             {[round](const perspective::Grid &g) { return round(g.centerY); },
              [](perspective::Grid &g, int v) { g.centerY = v; }});
    addField(tr("Cell"), 4, 2000, QStringLiteral(" px"), QStringLiteral("perspectiveCell"),
             {[round](const perspective::Grid &g) { return round(g.cell); },
              [](perspective::Grid &g, int v) { g.cell = v; }});
    addField(tr("Cells"), 1, 50, QString(), QStringLiteral("perspectiveCells"),
             {[](const perspective::Grid &g) { return g.cells; }, [](perspective::Grid &g, int v) { g.cells = v; }});

    m_color->setObjectName(QStringLiteral("perspectiveColor"));
    m_color->setIconSize(QSize(28, 14));
    m_color->setToolTip(tr("Color of the grid"));
    m_horizon->setObjectName(QStringLiteral("perspectiveHorizon"));
    m_horizon->setToolTip(tr("Show the plane's horizon line"));
    const int row = grid->rowCount();
    grid->addWidget(new QLabel(tr("Color"), this), row, 0);
    grid->addWidget(m_color, row, 1, Qt::AlignLeft);
    grid->addWidget(m_horizon, row, 2);

    connect(m_color, &QToolButton::clicked, this, [this] {
        const doc::Layer *layer = currentLayer();
        if (!layer)
            return;
        const QColor chosen = QColorDialog::getColor(QColor::fromRgba(layer->properties.perspective.color), this,
                                                     tr("Perspective grid color"), QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            edit([&chosen](perspective::Grid &g) { g.color = chosen.rgba(); }, false);
    });
    connect(m_horizon, &QCheckBox::toggled, this,
            [this](bool on) { edit([on](perspective::Grid &g) { g.horizon = on; }, false); });

    connect(m_document, &DocumentController::currentLayerChanged, this, &PerspectiveControls::sync);
    connect(m_document, &DocumentController::currentCelChanged, this, &PerspectiveControls::sync);
    connect(m_document, &DocumentController::currentFrameChanged, this, &PerspectiveControls::sync);
    connect(m_document, &DocumentController::structureChanged, this, &PerspectiveControls::sync);
    connect(m_document, &DocumentController::documentReplaced, this, &PerspectiveControls::sync);
    sync();
}

void PerspectiveControls::addField(const QString &label, int minimum, int maximum, const QString &suffix,
                                   const QString &name, Field field)
{
    auto *grid = static_cast<QGridLayout *>(layout());
    auto *slider = new QSlider(Qt::Horizontal, this);
    auto *spin = new QSpinBox(this);
    slider->setObjectName(name);
    spin->setObjectName(name + QStringLiteral("Value"));
    slider->setRange(minimum, maximum);
    spin->setRange(minimum, maximum);
    spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    const int row = grid->rowCount();
    grid->addWidget(new QLabel(label, this), row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(spin, row, 2);

    auto change = [field](int v) { return [field, v](perspective::Grid &g) { field.set(g, v); }; };
    // The slider shows the plane live while dragged, and is one undo step on release.
    connect(slider, &QSlider::valueChanged, this, [this, slider, spin, change](int v) {
        const QSignalBlocker block(spin);
        spin->setValue(v);
        edit(change(v), slider->isSliderDown());
    });
    connect(slider, &QSlider::sliderReleased, this,
            [this, slider, change] { edit(change(slider->value()), false); });
    connect(spin, &QSpinBox::valueChanged, this, [this, slider, change](int v) {
        const QSignalBlocker block(slider);
        slider->setValue(v);
        edit(change(v), false);
    });
    m_syncs.push_back([field, slider, spin](const perspective::Grid &g) {
        const QSignalBlocker b1(slider), b2(spin);
        slider->setValue(field.get(g));
        spin->setValue(field.get(g));
    });
}

std::optional<DocumentController::LayerRef> PerspectiveControls::currentRef() const
{
    const doc::Layer *layer = currentLayer();
    if (!layer)
        return std::nullopt;
    return DocumentController::LayerRef{m_document->currentCel(), m_document->currentDrawingId(), layer->id};
}

const doc::Layer *PerspectiveControls::currentLayer() const
{
    const doc::Drawing *drawing = m_document->currentDrawing();
    const doc::Layer *layer = drawing ? drawing->layer(m_document->currentLayer()) : nullptr;
    return layer && layer->kind == doc::LayerKind::Perspective ? layer : nullptr;
}

void PerspectiveControls::edit(const std::function<void(perspective::Grid &)> &change, bool preview)
{
    const auto ref = currentRef();
    if (!ref)
        return;
    const doc::Layer *layer = currentLayer();
    if (preview) {
        if (!m_before)
            m_before = layer->properties;
        doc::LayerProperties shown = layer->properties;
        change(shown.perspective);
        m_document->previewLayerProperties(*ref, shown);
        return;
    }
    // From before the drag, so undo takes it back in one go.
    doc::LayerProperties after = layer->properties;
    change(after.perspective);
    if (m_before) {
        m_document->previewLayerProperties(*ref, *m_before);
        m_before.reset();
    }
    m_document->execute(doc::makeSetLayerProperties(m_document->document(), ref->cel, ref->drawing, ref->layer, after));
}

void PerspectiveControls::sync()
{
    const doc::Layer *layer = currentLayer();
    setVisible(layer != nullptr);
    if (!layer || m_before)
        return;
    const perspective::Grid &g = layer->properties.perspective;
    for (const auto &s : m_syncs)
        s(g);
    m_color->setIcon(swatch(QColor::fromRgba(g.color)));
    const QSignalBlocker block(m_horizon);
    m_horizon->setChecked(g.horizon);
}

} // namespace mothion::ui
