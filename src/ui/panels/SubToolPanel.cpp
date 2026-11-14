#include "ui/panels/SubToolPanel.h"

#include "canvas/StrokeSession.h"
#include "core/brush/DabGenerator.h"
#include "tools/BrushLibrary.h"
#include "tools/SubToolMemory.h"

#include <QMenu>
#include <QMessageBox>
#include "canvas/TileQt.h"

#include <QEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
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
QImage renderFillPreview(tools::FillSettings::Mode mode, const QColor &ink, bool otherLayers)
{
    QImage image(kPreviewSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill = ink;
    fill.setAlphaF(0.45f);
    const QRectF r(QPointF(10, 6), QPointF(kPreviewSize.width() - 10, kPreviewSize.height() - 6));
    if (mode == tools::FillSettings::Mode::Bucket) {
        if (otherLayers) { // the lines of a layer behind, dashed, that stop the fill
            p.setPen(QPen(ink, 1.5, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.translated(-5, -3), 10, 10);
        }
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

// A blend or liquify sub tool: a few strokes of ink showing the effect.
QImage renderEffectPreview(const tools::BrushPreset &preset, const QColor &ink)
{
    QImage image(kPreviewSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(QPointF(10, 6), QPointF(kPreviewSize.width() - 10, kPreviewSize.height() - 6));
    const QPointF c = r.center();
    p.setBrush(Qt::NoBrush);
    if (preset.blendMode && *preset.blendMode == tools::BlendSettings::Mode::Smudge) {
        // a stroke whose end is dragged out and fades
        QLinearGradient g(r.left(), 0, r.right(), 0);
        g.setColorAt(0, ink);
        QColor clear = ink;
        clear.setAlpha(0);
        g.setColorAt(1, clear);
        p.setPen(QPen(QBrush(g), 8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(r.left() + 4, c.y()), QPointF(r.right() - 2, c.y() - 4));
    } else if (preset.blendMode) {
        // a hard edge and a soft one
        p.setPen(QPen(ink, 2));
        p.drawLine(QPointF(r.left(), c.y() - 6), QPointF(c.x() - 6, c.y() - 6));
        QColor soft = ink;
        for (int i = 0; i < 4; ++i) {
            soft.setAlphaF(0.8f - float(i) * 0.2f);
            p.setPen(QPen(soft, 2));
            p.drawLine(QPointF(c.x() + i * 7, c.y() + 6), QPointF(c.x() + i * 7 + 6, c.y() + 6));
        }
    } else if (preset.liquifyMode && *preset.liquifyMode == tools::LiquifySettings::Mode::Push) {
        // a line dragged by the pen
        p.setPen(QPen(ink, 2));
        QPainterPath line;
        line.moveTo(r.left() + 4, c.y() + 8);
        line.cubicTo(c.x() - 12, c.y() + 8, c.x() - 4, c.y() - 8, c.x() + 14, c.y() - 8);
        line.lineTo(r.right() - 4, c.y() - 8);
        p.drawPath(line);
    } else {
        // a circle with arrows out (expand) or in (shrink)
        const bool out = preset.liquifyMode && *preset.liquifyMode == tools::LiquifySettings::Mode::Expand;
        p.setPen(QPen(ink, 2));
        p.drawEllipse(c, 7, 7);
        for (int k = -1; k <= 1; k += 2) {
            const double x0 = c.x() + k * (out ? 10 : 30), x1 = c.x() + k * (out ? 30 : 10);
            p.drawLine(QPointF(x0, c.y()), QPointF(x1, c.y()));
            const double tip = out ? x1 : x1, back = k * (out ? -5 : 5);
            p.drawLine(QPointF(tip, c.y()), QPointF(tip + back, c.y() - 4));
            p.drawLine(QPointF(tip, c.y()), QPointF(tip + back, c.y() + 4));
        }
    }
    return image;
}

// A shape sub tool: the shape in outline.
QImage renderShapePreview(tools::ShapeSettings::Kind kind, const QColor &ink)
{
    QImage image(kPreviewSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(QPointF(10, 6), QPointF(kPreviewSize.width() - 10, kPreviewSize.height() - 6));
    p.setPen(QPen(ink, 2, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));
    p.setBrush(Qt::NoBrush);
    switch (kind) {
    case tools::ShapeSettings::Kind::Line: p.drawLine(r.bottomLeft() + QPointF(2, -2), r.topRight() + QPointF(-2, 2)); break;
    case tools::ShapeSettings::Kind::Rectangle: p.drawRect(r.adjusted(4, 2, -4, -2)); break;
    case tools::ShapeSettings::Kind::RoundedRectangle: p.drawRoundedRect(r.adjusted(4, 2, -4, -2), 6, 6); break;
    case tools::ShapeSettings::Kind::Ellipse: p.drawEllipse(r.adjusted(4, 1, -4, -1)); break;
    case tools::ShapeSettings::Kind::Polygon: {
        const QPointF c = r.center();
        p.drawPolygon(QPolygonF{QPointF(c.x() - 20, c.y() + 11), QPointF(c.x() - 12, c.y() - 11),
                                QPointF(c.x() + 8, c.y() - 7), QPointF(c.x() + 22, c.y() + 4),
                                QPointF(c.x() + 4, c.y() + 12)});
        break;
    }
    case tools::ShapeSettings::Kind::Curve: {
        QPainterPath curve;
        curve.moveTo(r.left() + 6, r.bottom() - 3);
        curve.cubicTo(r.left() + 30, r.top() - 8, r.right() - 40, r.bottom() + 8, r.right() - 6, r.top() + 3);
        p.drawPath(curve);
        break;
    }
    }
    return image;
}

// A selection sub tool: its marching-ants outline, a rectangle or a lasso loop.
QImage renderSelectionPreview(tools::ToolId tool, const QColor &ink)
{
    QImage image(kPreviewSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(QPointF(10, 6), QPointF(kPreviewSize.width() - 10, kPreviewSize.height() - 6));
    QPainterPath outline;
    if (tool == tools::ToolId::RectSelect) {
        outline.addRect(r);
    } else {
        outline.moveTo(r.left() + 8, r.center().y());
        outline.cubicTo(r.left(), r.top() - 4, r.center().x(), r.top() + 2, r.right() - 6, r.top() + 6);
        outline.cubicTo(r.right() + 4, r.center().y(), r.center().x() + 10, r.bottom() + 4, r.left() + 20, r.bottom());
        outline.closeSubpath();
    }
    p.setPen(QPen(ink, 1.5, Qt::DashLine));
    p.drawPath(outline);
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

SubToolPanel::SubToolPanel(tools::ToolState *state, tools::BrushLibrary *library, tools::SubToolMemory *memory,
                           QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_library(library)
    , m_memory(memory ? memory : new tools::SubToolMemory(state, library, {}, this))
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
        m_memory->select(m_presets[row]); // a paint sub tool comes back as it was left
    });
    connect(m_state, &tools::ToolState::activeToolChanged, this, &SubToolPanel::showPresetsFor);
    connect(m_library, &tools::BrushLibrary::presetsChanged, this, [this] {
        m_shownTool.reset(); // rebuilt whatever is shown
        showPresetsFor(m_state->activeTool());
    });

    // Each paint tool starts with its sub tool in use (SubToolMemory).
    showPresetsFor(m_state->activeTool());

    // An imported brush can be moved to another category (right click).
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &at) {
        const int row = m_list->row(m_list->itemAt(at));
        if (row < 0 || row >= m_presets.size() || m_presets[row].id.isEmpty())
            return;
        const tools::BrushPreset preset = m_presets[row];
        QMenu menu(this);
        // Back to what the sub tool starts with (size, opacity, curve, stabilization, corrections).
        QAction *reset = menu.addAction(tr("Reset to default"));
        reset->setEnabled(m_memory->isModified(preset));
        connect(reset, &QAction::triggered, this, [this, preset] { m_memory->reset(preset); });
        if (preset.storedFile.isEmpty()) {
            menu.exec(m_list->viewport()->mapToGlobal(at));
            return;
        }
        QMenu *move = menu.addMenu(tr("Move to"));
        for (const tools::ToolId category : tools::kBrushTools) {
            QAction *action = move->addAction(tools::ToolState::displayName(category));
            action->setEnabled(category != preset.tool);
            connect(action, &QAction::triggered, this, [this, preset, category] {
                QString error;
                if (!m_library->moveToCategory(preset, category, &error) && !error.isEmpty())
                    QMessageBox::warning(this, tr("Move brush"), error);
            });
        }
        menu.exec(m_list->viewport()->mapToGlobal(at));
    });
}

void SubToolPanel::showPresetsFor(tools::ToolId tool)
{
    // The two selection tools share their sub tools: picking one of them
    // only moves the highlight (and does not rebuild the list under its click).
    auto isSelection = [](tools::ToolId t) { return t == tools::ToolId::RectSelect || t == tools::ToolId::LassoSelect; };
    if (m_shownTool && isSelection(*m_shownTool) && isSelection(tool) && !m_presets.isEmpty()) {
        m_shownTool = tool;
        for (int i = 0; i < m_presets.size(); ++i)
            if (m_presets[i].selectsTool == tool)
                m_list->setCurrentRow(i);
        return;
    }
    m_shownTool = tool;
    m_presets = m_library->presets(tool);
    m_list->clear();
    const QColor ink = palette().text().color();
    for (const tools::BrushPreset &preset : std::as_const(m_presets)) {
        auto *item = new QListWidgetItem(preset.name, m_list);
        item->setData(kPreviewRole, preset.selectsTool ? renderSelectionPreview(*preset.selectsTool, ink)
                                    : preset.fillMode  ? renderFillPreview(*preset.fillMode, ink, preset.fillAllLayers.value_or(false))
                                    : preset.shapeKind ? renderShapePreview(*preset.shapeKind, ink)
                                    : (preset.blendMode || preset.liquifyMode) ? renderEffectPreview(preset, ink)
                                                       : renderPreview(preset, ink));
    }
    m_list->setVisible(!m_presets.isEmpty());
    m_empty->setVisible(m_presets.isEmpty());
    if (m_presets.isEmpty())
        return;
    int row = m_selected.count(tool) ? m_selected[tool] : 0;
    const QString current = m_memory->current(tool); // a paint tool's sub tool in use
    for (int i = 0; i < m_presets.size(); ++i) { // what is in use, whoever set it
        const tools::BrushPreset &p = m_presets[i];
        if ((p.shapeKind && *p.shapeKind == m_state->shapeSettings().kind)
            || (p.blendMode && *p.blendMode == m_state->blendSettings().mode)
            || (p.liquifyMode && *p.liquifyMode == m_state->liquifySettings().mode)
            || (p.fillMode && *p.fillMode == m_state->fillSettings().mode
                && (!p.fillAllLayers || *p.fillAllLayers == m_state->fillSettings().allLayers))
            || (p.selectsTool && *p.selectsTool == tool) || (!current.isEmpty() && p.id == current))
            row = i;
    }
    m_list->setCurrentRow(row);
}

void SubToolPanel::changeEvent(QEvent *event)
{
    // The previews are drawn in the text color: redraw them after a theme switch.
    if (event->type() == QEvent::PaletteChange) {
        m_shownTool.reset();
        showPresetsFor(m_state->activeTool());
    }
    QWidget::changeEvent(event);
}

} // namespace mothion::ui
