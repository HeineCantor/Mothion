#include "canvas/FillTool.h"

#include "canvas/SelectionMask.h"
#include "canvas/SelectionTool.h"
#include "core/fill/FloodFill.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

#include <QLineF>
#include <QPainterPath>

#include <cmath>
#include <vector>

namespace mothion::canvas {

namespace {

// `fill` × `coverage` (0..255) put behind `pixel`, premultiplied: what
// is already there stays on top, so lines on the same layer keep their edge.
doc::Pixel behind(doc::Pixel pixel, doc::Pixel fill, int coverage)
{
    doc::Pixel out = 0;
    const int a = int((pixel >> 24) & 0xff);
    for (int shift = 0; shift < 32; shift += 8) {
        const int f = (int((fill >> shift) & 0xff) * coverage + 127) / 255;
        const int p = int((pixel >> shift) & 0xff);
        out |= doc::Pixel(p + (f * (255 - a) + 127) / 255) << shift;
    }
    return out;
}

// `pixel` with `amount` (0..255) of it taken away: what a fill does to a mask (it shows the layer where it fills).
doc::Pixel clearedBy(doc::Pixel pixel, int amount)
{
    doc::Pixel out = 0;
    for (int shift = 0; shift < 32; shift += 8)
        out |= doc::Pixel((int((pixel >> shift) & 0xff) * (255 - amount) + 127) / 255) << shift;
    return out;
}

// `fill` × `coverage` (0..255) over `pixel`, premultiplied.
doc::Pixel over(doc::Pixel pixel, doc::Pixel fill, int coverage)
{
    doc::Pixel out = 0;
    const int a = (int((fill >> 24) & 0xff) * coverage + 127) / 255;
    for (int shift = 0; shift < 32; shift += 8) {
        const int f = (int((fill >> shift) & 0xff) * coverage + 127) / 255;
        const int p = int((pixel >> shift) & 0xff);
        out |= doc::Pixel(f + (p * (255 - a) + 127) / 255) << shift;
    }
    return out;
}

} // namespace

FillTool::FillTool(document::DocumentController *document, tools::ToolState *state, Frame frame,
                   std::function<void(const QString &)> status)
    : m_document(document)
    , m_state(state)
    , m_frame(std::move(frame))
    , m_status(std::move(status))
{
}

void FillTool::press(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (m_state->fillSettings().mode == tools::FillSettings::Mode::Lasso)
        m_lasso = {docPos};
    else
        fillAround(docPos);
}

void FillTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_lasso.isEmpty() && QLineF(m_lasso.last(), docPos).length() >= 1.0)
        m_lasso << docPos;
}

void FillTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (m_lasso.isEmpty())
        return;
    move(docPos, modifiers);
    fillLasso();
    m_lasso.clear();
}

void FillTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    if (m_lasso.isEmpty())
        return;
    QPainterPath open; // the closing line only comes with the release
    open.addPolygon(m_lasso);
    paintSelectionOutline(painter, view, open);
}

doc::Pixel FillTool::fillColor(const QColor &c) const
{
    const double opacity = m_state->settings(tools::ToolId::Fill).opacity;
    const int alpha = int(std::lround(255 * opacity));
    return doc::Pixel(alpha) << 24 | doc::Pixel(c.red() * alpha / 255) << 16 | doc::Pixel(c.green() * alpha / 255) << 8
           | doc::Pixel(c.blue() * alpha / 255);
}

void FillTool::fillLasso()
{
    if (m_lasso.size() < 3)
        return;
    QPainterPath area;
    area.addPolygon(m_lasso);
    area.closeSubpath();
    if (m_document->hasSelection())
        area = area.intersected(m_document->selection());
    const doc::Document &d = m_document->document();
    QPainterPath page;
    page.addRect(0, 0, d.width(), d.height());
    area = area.intersected(page);
    const SelectionMask mask = SelectionMask::fromPath(area);
    if (mask.isEmpty())
        return;

    QString reason;
    const auto target = m_document->beginPaint(true, &reason);
    if (!target) {
        if (!reason.isEmpty())
            m_status(reason);
        return;
    }
    const doc::TiledImage before = *target->image;
    const QRect box = mask.bounds;
    const doc::Pixel color = fillColor(target->tint(m_state->color()));
    std::vector<doc::Pixel> pixels(std::size_t(box.width()) * std::size_t(box.height()));
    target->image->read(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    for (int y = 0; y < box.height(); ++y)
        for (int x = 0; x < box.width(); ++x)
            if (const int coverage = mask.at(box.x() + x, box.y() + y)) {
                doc::Pixel &p = pixels[std::size_t(y) * std::size_t(box.width()) + std::size_t(x)];
                p = target->mask ? clearedBy(p, coverage * int(color >> 24) / 255) : over(p, color, coverage);
            }
    target->image->write(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    m_document->notifyPainted(box);
    m_document->endPaint(before, doc::CommandKind::Fill);
}

void FillTool::fillAround(const QPointF &docPos)
{
    const doc::Document &d = m_document->document();
    const int w = d.width(), h = d.height();
    const int seedX = int(std::floor(docPos.x())), seedY = int(std::floor(docPos.y()));
    if (seedX < 0 || seedY < 0 || seedX >= w || seedY >= h)
        return;
    const SelectionMask selection = SelectionMask::fromPath(m_document->selection());
    if (m_document->hasSelection() && selection.at(seedX, seedY) == 0)
        return m_status(QObject::tr("Click inside the selection to fill it"));

    QString reason;
    const auto target = m_document->beginPaint(true, &reason);
    if (!target) {
        if (!reason.isEmpty())
            m_status(reason);
        return;
    }
    const doc::TiledImage before = *target->image;

    // Where to look for the area: this layer, or everything shown.
    const tools::FillSettings settings = m_state->fillSettings();
    std::vector<doc::Pixel> source(std::size_t(w) * std::size_t(h));
    const QImage &frame = m_frame();
    if (settings.allLayers && frame.size() == QSize(w, h)
        && frame.format() == QImage::Format_ARGB32_Premultiplied) {
        for (int y = 0; y < h; ++y) {
            const auto *line = reinterpret_cast<const doc::Pixel *>(frame.constScanLine(y));
            std::copy_n(line, w, source.begin() + std::ptrdiff_t(y) * w);
        }
    } else {
        before.read(0, 0, w, h, source.data(), w);
    }
    fill::Region region = fill::floodRegion(source.data(), w, h, seedX, seedY, settings.tolerance);
    fill::grow(region, settings.expand);
    if (region.isEmpty()) {
        m_document->cancelPaint();
        return;
    }

    // The color behind what is there, inside the area (and the selection).
    const doc::Pixel color = fillColor(target->tint(m_state->color()));
    const QRect box(QPoint(region.left, region.top), QPoint(region.right, region.bottom));
    std::vector<doc::Pixel> pixels(std::size_t(box.width()) * std::size_t(box.height()));
    target->image->read(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    for (int y = 0; y < box.height(); ++y) {
        for (int x = 0; x < box.width(); ++x) {
            const int dx = box.x() + x, dy = box.y() + y;
            int coverage = region.at(dx, dy);
            if (m_document->hasSelection())
                coverage = coverage * selection.at(dx, dy) / 255;
            doc::Pixel &p = pixels[std::size_t(y) * std::size_t(box.width()) + std::size_t(x)];
            if (coverage)
                p = target->mask ? clearedBy(p, coverage * int(color >> 24) / 255) : behind(p, color, coverage);
        }
    }
    target->image->write(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    m_document->notifyPainted(box);
    m_document->endPaint(before, doc::CommandKind::Fill);
}

} // namespace mothion::canvas
