#include "canvas/FillTool.h"

#include "canvas/SelectionMask.h"
#include "core/fill/FloodFill.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

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
    const QColor c = m_state->color();
    const double opacity = m_state->settings(tools::ToolId::Fill).opacity;
    const int alpha = int(std::lround(255 * opacity));
    const doc::Pixel color = doc::Pixel(alpha) << 24 | doc::Pixel(c.red() * alpha / 255) << 16
                             | doc::Pixel(c.green() * alpha / 255) << 8 | doc::Pixel(c.blue() * alpha / 255);
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
                p = behind(p, color, coverage);
        }
    }
    target->image->write(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    m_document->notifyPainted(box);
    m_document->endPaint(before, doc::CommandKind::Fill);
}

} // namespace mothion::canvas
