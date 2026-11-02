#include "ui/panels/SubToolPanel.h"

#include "canvas/StrokeSession.h"
#include "core/brush/DabGenerator.h"
#include "tools/BrushLibrary.h"
#include "canvas/TileQt.h"

#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mothion::ui {

namespace {

constexpr int kRowHeight = 44;
constexpr QSize kPreviewSize(118, 40); // leaves room for the name on the right
constexpr int kPreviewRole = Qt::UserRole + 1;

// Draws an S-shaped stroke with a press-and-lift pressure profile using the
// real brush engine, so the preview shows what the brush does.
QImage renderPreview(const tools::BrushPreset &preset, const QColor &ink)
{
    doc::TiledImage image;
    const QRect area(QPoint(0, 0), kPreviewSize);
    const tools::BrushDefinitionPtr definition = preset.brush ? preset.brush : tools::roundBrush();
    brush::BrushStyle style = definition->style;
    // Big brushes are scaled down to fit the row, keeping their pressure response.
    style.size = std::min(preset.size, definition->tips.isEmpty() ? 22.0 : 36.0) / 2.0;
    if (style.spray)
        style.size *= 0.5;
    canvas::StrokeSession session(
        image, std::make_shared<canvas::DabStamper>(ink, style.hardness, definition->tips, style.verticalThickness),
        preset.opacity, false, area);
    constexpr int kSteps = 60;
    std::vector<brush::BrushInput> points;
    for (int i = 0; i <= kSteps; ++i) {
        const double t = double(i) / kSteps;
        brush::BrushInput in;
        in.x = 8 + t * (kPreviewSize.width() - 16);
        in.y = kPreviewSize.height() / 2.0 - std::sin(t * 2 * std::numbers::pi) * 7;
        in.pressure = preset.curve.map(float(std::sin(t * std::numbers::pi))); // 0 -> 1 -> 0
        in.velocity = 0.3;
        points.push_back(in);
    }
    session.paintStroke(style, 12345, std::move(points)); // fixed seed: the preview never changes
    return canvas::toQImage(image, area);
}

// A fill sub tool: a closed shape filled (bucket), or a lasso loop with its
// inside filled (lasso).
QImage renderFillPreview(tools::FillSettings::Mode mode, const QColor &ink)
{
    QImage image(kPreviewSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill = ink;
    fill.setAlphaF(0.45);
    const QRectF r(QPointF(10, 6), QPointF(kPreviewSize.width() - 10, kPreviewSize.height() - 6));
    if (mode == tools::FillSettings::Mode::Bucket) {
        p.setPen(QPen(ink, 2));
        p.setBrush(fill);
        p.drawRoundedRect(r, 10, 10);
    } else {
        QPainterPath loop;
        loop.moveTo(r.left() + 8, r.center().y());
        loop.cubicTo(r.left(), r.top() - 4, r.center().x(), r.top() + 2, r.right() - 6, r.top() + 6);
        loop.cubicTo(r.right() + 4, r.center().y(), r.center().x() + 10, r.bottom() + 4, r.left() + 20, r.bottom());
        loop.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(loop);
        QPen dashes(ink, 1.5, Qt::DashLine);
        p.setPen(dashes);
        p.setBrush(Qt::NoBrush);
        p.drawPath(loop);
    }
    return image;
}

// One row: stroke preview on the left, preset name bottom-right.
class PresetDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {option.rect.width(), kRowHeight};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QRect r = option.rect.adjusted(1, 1, -1, -1);
        const bool selected = option.state & QStyle::State_Selected;
        // Selection: a tint of the highlight over the base, so the stroke and
        // name keep their contrast in either theme.
        const QColor base = option.palette.base().color();
        const QColor highlight = option.palette.highlight().color();
        auto mix = [](int a, int b) { return (a * 7 + b * 3) / 10; };
        const QColor tint(mix(base.red(), highlight.red()), mix(base.green(), highlight.green()),
                          mix(base.blue(), highlight.blue()));
        p->fillRect(r, selected ? tint : base);
        p->setPen(option.palette.mid().color());
        p->drawLine(r.bottomLeft(), r.bottomRight());

        const QImage preview = index.data(kPreviewRole).value<QImage>();
        p->drawImage(QPoint(r.left() + 2, r.top() + (r.height() - preview.height()) / 2), preview);

        p->setPen(option.palette.text().color());
        const QRect textRect = r.adjusted(kPreviewSize.width() + 6, 0, -6, -3);
        const QString name = option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, textRect.width());
        p->drawText(textRect, Qt::AlignRight | Qt::AlignBottom, name);
    }
};

} // namespace

SubToolPanel::SubToolPanel(tools::ToolState *state, tools::BrushLibrary *library, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_library(library)
    , m_list(new QListWidget(this))
    , m_empty(new QLabel(tr("No sub tools"), this))
{
    m_list->setItemDelegate(new PresetDelegate(m_list));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFrameShape(QFrame::NoFrame);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_empty, 1);

    // Only user clicks apply a preset: switching tools must not reset the
    // settings the user has tweaked since.
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int row = m_list->row(item);
        m_selected[m_state->activeTool()] = row;
        tools::applyPreset(*m_state, m_presets[row]);
    });
    connect(m_state, &tools::ToolState::activeToolChanged, this, &SubToolPanel::showPresetsFor);
    connect(m_library, &tools::BrushLibrary::presetsChanged, this,
            [this] { showPresetsFor(m_state->activeTool()); });

    // Start with the first brush preset applied, so the defaults are a real sub tool.
    const auto brushPresets = tools::builtInPresets(tools::ToolId::Brush);
    if (!brushPresets.isEmpty())
        tools::applyPreset(*m_state, brushPresets.first());
    showPresetsFor(m_state->activeTool());
}

void SubToolPanel::showPresetsFor(tools::ToolId tool)
{
    m_presets = m_library->presets(tool);
    m_list->clear();
    const QColor ink = palette().text().color();
    for (const tools::BrushPreset &preset : std::as_const(m_presets)) {
        auto *item = new QListWidgetItem(preset.name, m_list);
        item->setData(kPreviewRole,
                      preset.fillMode ? renderFillPreview(*preset.fillMode, ink) : renderPreview(preset, ink));
    }
    m_list->setVisible(!m_presets.isEmpty());
    m_empty->setVisible(m_presets.isEmpty());
    if (m_presets.isEmpty())
        return;
    int row = m_selected.count(tool) ? m_selected[tool] : 0;
    for (int i = 0; i < m_presets.size(); ++i) // a fill sub tool: the mode in use, whoever set it
        if (m_presets[i].fillMode && *m_presets[i].fillMode == m_state->fillSettings().mode)
            row = i;
    m_list->setCurrentRow(row);
}

void SubToolPanel::changeEvent(QEvent *event)
{
    // The previews are drawn in the text color: redraw them after a theme switch.
    if (event->type() == QEvent::PaletteChange)
        showPresetsFor(m_state->activeTool());
    QWidget::changeEvent(event);
}

} // namespace mothion::ui
