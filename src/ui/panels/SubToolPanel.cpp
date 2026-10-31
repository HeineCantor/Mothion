#include "ui/panels/SubToolPanel.h"

#include "canvas/RasterSurface.h"
#include "canvas/StrokeRenderer.h"

#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <cmath>
#include <numbers>

namespace mothion::ui {

namespace {

constexpr int kRowHeight = 44;
constexpr QSize kPreviewSize(118, 40); // leaves room for the name on the right
constexpr int kPreviewRole = Qt::UserRole + 1;

// Draws an S-shaped stroke with a press-and-lift pressure profile using the
// actual StrokeRenderer + RasterSurface, so the preview shows what the brush does.
QImage renderPreview(const tools::BrushPreset &preset, const QColor &ink)
{
    canvas::RasterSurface surface(kPreviewSize);
    canvas::StrokeRenderer renderer;
    surface.beginStroke(ink, preset.opacity, false);
    // Big brushes are scaled down to fit the row, keeping their pressure response.
    const double maxRadius = std::min(preset.size, 22.0) / 2.0;
    constexpr int kSteps = 60;
    for (int i = 0; i <= kSteps; ++i) {
        const double t = double(i) / kSteps;
        const QPointF pos(8 + t * (kPreviewSize.width() - 16),
                          kPreviewSize.height() / 2.0 - std::sin(t * 2 * std::numbers::pi) * 7);
        const float pressure = float(std::sin(t * std::numbers::pi)); // 0 -> 1 -> 0
        const canvas::StrokeRenderer::Point p{pos, std::max(0.4, maxRadius * preset.curve.map(pressure))};
        i == 0 ? renderer.begin(p, surface) : renderer.continueTo(p, surface);
    }
    surface.endStroke();
    return surface.image();
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
        p->fillRect(r, selected ? option.palette.highlight().color().darker(160) : option.palette.base().color());
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

SubToolPanel::SubToolPanel(tools::ToolState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_list(new QListWidget(this))
    , m_empty(new QLabel(tr("Nessun sotto-strumento"), this))
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

    // Start with the first brush preset applied, so the defaults are a real sub tool.
    const auto brushPresets = tools::builtInPresets(tools::ToolId::Brush);
    if (!brushPresets.isEmpty())
        tools::applyPreset(*m_state, brushPresets.first());
    showPresetsFor(m_state->activeTool());
}

void SubToolPanel::showPresetsFor(tools::ToolId tool)
{
    m_presets = tools::builtInPresets(tool);
    m_list->clear();
    const QColor ink = palette().text().color();
    for (const tools::BrushPreset &preset : std::as_const(m_presets)) {
        auto *item = new QListWidgetItem(preset.name, m_list);
        item->setData(kPreviewRole, renderPreview(preset, ink));
    }
    m_list->setVisible(!m_presets.isEmpty());
    m_empty->setVisible(m_presets.isEmpty());
    if (!m_presets.isEmpty())
        m_list->setCurrentRow(m_selected.count(tool) ? m_selected[tool] : 0);
}

} // namespace mothion::ui
