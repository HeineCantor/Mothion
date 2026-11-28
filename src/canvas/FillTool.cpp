#include "canvas/FillTool.h"

#include "canvas/SelectionMask.h"
#include "canvas/SelectionTool.h"
#include "canvas/VectorRender.h"
#include "core/fill/FillShape.h"
#include "core/fill/FloodFill.h"
#include "core/puppet/Contour.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

#include <QLineF>
#include <QPainterPath>

#include <algorithm>
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

// `fill` × `coverage` (0..255) laid over `pixel`, premultiplied: what is there is covered (an area filled before
// takes the new color).
doc::Pixel over(doc::Pixel pixel, doc::Pixel fill, int coverage)
{
    doc::Pixel out = 0;
    const int a = (int((fill >> 24) & 0xff) * coverage + 127) / 255;
    for (int shift = 0; shift < 32; shift += 8) {
        const int f = (int((fill >> shift) & 0xff) * coverage + 127) / 255;
        const int p = int((pixel >> shift) & 0xff);
        out |= doc::Pixel(std::min(255, f + (p * (255 - a) + 127) / 255)) << shift;
    }
    return out;
}

// The tiles of `image`, as one rectangle.
QRect tileBounds(const doc::TiledImage &image)
{
    QRect r;
    for (const doc::TileCoord &c : image.tileCoords())
        r |= QRect(c.x * doc::kTileSize, c.y * doc::kTileSize, doc::kTileSize, doc::kTileSize);
    return r;
}

// `pixel` with `amount` (0..255) of it taken away: what a fill does to a mask (it shows the layer where it fills).
doc::Pixel clearedBy(doc::Pixel pixel, int amount)
{
    doc::Pixel out = 0;
    for (int shift = 0; shift < 32; shift += 8)
        out |= doc::Pixel((int((pixel >> shift) & 0xff) * (255 - amount) + 127) / 255) << shift;
    return out;
}

} // namespace

FillTool::FillTool(document::DocumentController *document, tools::ToolState *state, Frame frame,
                   std::function<void(const QString &)> status, Copies copies)
    : m_document(document)
    , m_state(state)
    , m_frame(std::move(frame))
    , m_status(std::move(status))
    , m_copies(std::move(copies))
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
    if (m_copies) // the outline's copies round the symmetry's axes too
        for (const QTransform &t : m_copies(m_lasso.first())) {
            QPainterPath copy;
            copy.addPolygon(t.map(m_lasso));
            copy.closeSubpath();
            area = area.united(copy);
        }
    if (m_document->hasAreaSelection())
        area = area.intersected(m_document->areaSelection());
    const SelectionMask mask = SelectionMask::fromPath(area);
    if (mask.isEmpty())
        return;

    QString reason;
    const bool erase = m_state->transparentSelected(); // the transparent color takes away, as with the brush
    const auto target = m_document->beginPaint(!erase, &reason, true, true);
    if (!target) {
        if (!reason.isEmpty())
            m_status(reason);
        return;
    }
    const doc::TiledImage before = *target->image;
    if (target->vector && erase) {
        eraseShape(*target, area, true, before);
        return;
    }
    if (target->vector) { // the outline is the shape
        const QColor c = target->tint(m_state->color());
        putFill(*target, fillShaped(makeFill({}, c.rgba(), m_state->settings(tools::ToolId::Fill).opacity), area), before);
        return;
    }
    const QRect box = mask.bounds;
    const doc::Pixel color = fillColor(target->tint(m_state->color()));
    fillByMask(*target->image, mask, color, erase != target->mask); // (on a mask filling shows, erasing hides)
    m_document->notifyPainted(box);
    m_document->endPaint(before, doc::CommandKind::Fill);
}

void FillTool::fillAround(const QPointF &docPos)
{
    const int seedX = int(std::floor(docPos.x())), seedY = int(std::floor(docPos.y()));
    const tools::FillSettings settings = m_state->fillSettings();
    // Where to look for the area: this layer, or everything shown; only as far as the canvas is seen.
    const Shown shown = m_frame(QPoint(seedX, seedY), settings.allLayers);
    const QRect area = shown.area;
    if (!area.contains(seedX, seedY))
        return;
    const int w = area.width(), h = area.height();
    const SelectionMask selection = SelectionMask::fromPath(m_document->areaSelection());
    if (m_document->hasAreaSelection() && selection.at(seedX, seedY) == 0)
        return m_status(QObject::tr("Click inside the selection to fill it"));

    QString reason;
    const bool erase = m_state->transparentSelected(); // the transparent color takes away, as with the brush
    const auto target = m_document->beginPaint(!erase, &reason, true, true);
    if (!target) {
        if (!reason.isEmpty())
            m_status(reason);
        return;
    }
    const doc::TiledImage before = *target->image;

    std::vector<doc::Pixel> source(std::size_t(w) * std::size_t(h));
    const QImage &frame = shown.image;
    if (settings.allLayers && frame.size() == area.size() && frame.format() == QImage::Format_ARGB32_Premultiplied) {
        for (int y = 0; y < h; ++y) {
            const auto *line = reinterpret_cast<const doc::Pixel *>(frame.constScanLine(y));
            std::copy_n(line, w, source.begin() + std::ptrdiff_t(y) * w);
        }
    } else {
        before.read(area.x(), area.y(), w, h, source.data(), w);
    }
    // The area round the click, and round each point its copies fall on while the symmetry is on, all found in the
    // drawing as it is before any of them is filled: what each covers (grown under the lines round it), and the area
    // itself (`clicked`).
    std::vector<QPoint> seeds{QPoint(seedX, seedY)};
    if (m_copies)
        for (const QTransform &t : m_copies(docPos)) {
            const QPointF at = t.map(docPos);
            seeds.emplace_back(int(std::floor(at.x())), int(std::floor(at.y())));
        }
    fill::Region region;
    region.width = w;
    region.height = h;
    region.mask.assign(std::size_t(w) * std::size_t(h), 0);
    std::vector<std::uint8_t> clicked(std::size_t(w) * std::size_t(h), 0);
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        const QPoint seed = seeds[i] - area.topLeft();
        if (seed.x() < 0 || seed.y() < 0 || seed.x() >= w || seed.y() >= h)
            continue;
        if (i > 0 && m_document->hasAreaSelection() && selection.at(seeds[i].x(), seeds[i].y()) == 0)
            continue; // a copy out of the selection
        if (clicked[std::size_t(seed.y()) * std::size_t(w) + std::size_t(seed.x())])
            continue; // in an area found already
        fill::Region one = fill::floodRegion(source.data(), w, h, seed.x(), seed.y(), settings.tolerance);
        if (one.isEmpty())
            continue;
        for (int y = one.top; y <= one.bottom; ++y)
            for (int x = one.left; x <= one.right; ++x)
                if (one.at(x, y))
                    clicked[std::size_t(y) * std::size_t(w) + std::size_t(x)] = 255;
        fill::grow(one, settings.expand);
        for (int y = one.top; y <= one.bottom; ++y)
            for (int x = one.left; x <= one.right; ++x) {
                std::uint8_t &m = region.mask[std::size_t(y) * std::size_t(w) + std::size_t(x)];
                m = std::max(m, one.at(x, y));
            }
        if (region.isEmpty()) {
            region.left = one.left, region.top = one.top, region.right = one.right, region.bottom = one.bottom;
        } else {
            region.left = std::min(region.left, one.left);
            region.top = std::min(region.top, one.top);
            region.right = std::max(region.right, one.right);
            region.bottom = std::max(region.bottom, one.bottom);
        }
    }
    if (region.isEmpty()) {
        m_document->cancelPaint();
        return;
    }

    if (target->vector) {
        // A shape of colour: the area clicked, grown under the lines round it up to their middle (not by the expand
        // setting: its edge then stays under the lines however they are moved), traced as outlines.
        fill::Region area0;
        area0.width = w;
        area0.height = h;
        area0.mask = clicked;
        area0.left = w;
        area0.top = h;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (clicked[std::size_t(y) * std::size_t(w) + std::size_t(x)]) {
                    area0.left = std::min(area0.left, x);
                    area0.right = std::max(area0.right, x);
                    area0.top = std::min(area0.top, y);
                    area0.bottom = std::max(area0.bottom, y);
                }
        const doc::Pixel seedPixel = source[std::size_t(seedY - area.y()) * std::size_t(w) + std::size_t(seedX - area.x())];
        constexpr int kReach = 64; // px: the widest line a fill goes under to its middle
        const fill::Region grown = fill::grownToLineMiddle(area0, source.data(), seedPixel, settings.tolerance, kReach);
        std::vector<std::vector<QPointF>> outlines;
        for (const std::vector<fill::Point> &outline : fill::regionOutlines(grown)) {
            std::vector<puppet::Vec2> points;
            for (const fill::Point &p : outline)
                points.push_back({p.x + area.x(), p.y + area.y()});
            std::vector<QPointF> simple;
            for (const puppet::Vec2 &p : puppet::simplifyContour(points, 0.7))
                simple.emplace_back(p.x, p.y);
            outlines.push_back(std::move(simple));
        }
        doc::VectorStroke shape = makeFill(outlines, target->tint(m_state->color()).rgba(),
                                           m_state->settings(tools::ToolId::Fill).opacity);
        if (erase) {
            QPainterPath gone = fillPath(shape);
            if (m_document->hasAreaSelection())
                gone = gone.intersected(m_document->areaSelection());
            eraseShape(*target, gone, false, before);
            return;
        }
        if (m_document->hasAreaSelection())
            shape = fillShaped(shape, fillPath(shape).intersected(m_document->areaSelection()));
        putFill(*target, std::move(shape), before);
        return;
    }

    // Inside the area clicked (and the selection) the color over what is there, so an area filled before is filled
    // again; where the area grows under the lines round it, behind what is there, so the lines keep their edge.
    const doc::Pixel color = fillColor(target->tint(m_state->color()));
    const QRect box = QRect(QPoint(region.left, region.top), QPoint(region.right, region.bottom)).translated(area.topLeft());
    std::vector<doc::Pixel> pixels(std::size_t(box.width()) * std::size_t(box.height()));
    target->image->read(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    for (int y = 0; y < box.height(); ++y) {
        for (int x = 0; x < box.width(); ++x) {
            const int dx = box.x() + x, dy = box.y() + y;
            int coverage = region.at(dx - area.x(), dy - area.y());
            if (m_document->hasAreaSelection())
                coverage = coverage * selection.at(dx, dy) / 255;
            doc::Pixel &p = pixels[std::size_t(y) * std::size_t(box.width()) + std::size_t(x)];
            if (!coverage)
                continue;
            const bool inArea = clicked[std::size_t(dy - area.y()) * std::size_t(w) + std::size_t(dx - area.x())];
            if (erase) { // of the area itself: the lines round it, which it grows under, stay
                if (target->mask)
                    p = over(p, color, coverage);
                else if (inArea)
                    p = clearedBy(p, coverage * int(color >> 24) / 255);
            } else if (target->mask)
                p = clearedBy(p, coverage * int(color >> 24) / 255);
            else if (inArea)
                p = over(p, color, coverage);
            else
                p = behind(p, color, coverage);
        }
    }
    target->image->write(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    m_document->notifyPainted(box);
    m_document->endPaint(before, doc::CommandKind::Fill);
}

void FillTool::eraseShape(const document::DocumentController::PaintTarget &target, const QPainterPath &area, bool linesToo,
                          const doc::TiledImage &before)
{
    const doc::Layer *layer = m_document->document().layer(target.cel, target.drawing, target.layer);
    if (!layer || area.isEmpty()) {
        m_document->cancelPaint();
        return;
    }
    const SelectionMask mask = SelectionMask::fromPath(area);
    std::vector<doc::VectorStroke> after;
    for (const doc::VectorStroke &stroke : layer->strokes) {
        if (!stroke.fill && !linesToo) {
            after.push_back(stroke);
            continue;
        }
        const SplitStroke cut = splitStrokeByMask(stroke, mask, area);
        after.insert(after.end(), cut.outside.begin(), cut.outside.end());
    }
    if (after == layer->strokes) { // nothing there
        m_document->cancelPaint();
        return;
    }
    const QRect was = tileBounds(before);
    m_document->attachStrokeChange(target, layer->strokes, after);
    doc::TiledImage drawn;
    const QRect painted = drawStrokes(drawn, after, *m_state, {});
    *target.image = std::move(drawn);
    m_document->notifyPainted(was | painted);
    m_document->endPaint(before, doc::CommandKind::Fill);
}

void FillTool::putFill(const document::DocumentController::PaintTarget &target, doc::VectorStroke fill,
                       const doc::TiledImage &before)
{
    const doc::Layer *layer = m_document->document().layer(target.cel, target.drawing, target.layer);
    if (!layer || fill.points.empty()) {
        m_document->cancelPaint();
        return;
    }
    // Above the fills under the first line (an area filled again takes the new colour), under the lines.
    std::vector<doc::VectorStroke> after = layer->strokes;
    std::size_t at = 0;
    while (at < after.size() && after[at].fill)
        ++at;
    after.insert(after.begin() + std::ptrdiff_t(at), std::move(fill));
    const QRect was = tileBounds(before);
    m_document->attachStrokeChange(target, layer->strokes, after);
    doc::TiledImage drawn;
    const QRect painted = drawStrokes(drawn, after, *m_state, {});
    *target.image = std::move(drawn);
    m_document->notifyPainted(was | painted);
    m_document->endPaint(before, doc::CommandKind::Fill);
}

} // namespace mothion::canvas
