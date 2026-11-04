#include "canvas/EffectBrushTools.h"

#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>

namespace mothion::canvas {

namespace {

using Color = std::array<float, 4>; // premultiplied A, R, G, B in 0..255

Color unpack(doc::Pixel p)
{
    return {float((p >> 24) & 0xff), float((p >> 16) & 0xff), float((p >> 8) & 0xff), float(p & 0xff)};
}

doc::Pixel pack(const Color &c)
{
    auto byte = [](float v) { return doc::Pixel(std::clamp(int(std::lround(v)), 0, 255)); };
    const doc::Pixel a = byte(c[0]);
    return a << 24 | std::min(byte(c[1]), a) << 16 | std::min(byte(c[2]), a) << 8 | std::min(byte(c[3]), a);
}

Color mix(const Color &from, const Color &to, double amount)
{
    Color out;
    for (std::size_t k = 0; k < 4; ++k)
        out[k] = float(from[k] + (to[k] - from[k]) * amount);
    return out;
}

} // namespace

EffectBrushTool::EffectBrushTool(document::DocumentController *document, tools::ToolState *state,
                                 tools::ToolId tool, doc::CommandKind kind, QString nothingToDo,
                                 std::function<void(const QString &)> status)
    : m_document(document)
    , m_state(state)
    , m_tool(tool)
    , m_kind(kind)
    , m_nothingToDo(std::move(nothingToDo))
    , m_status(std::move(status))
{
}

double EffectBrushTool::radius() const
{
    return std::max(0.5, m_state->settings(m_tool).size / 2);
}

double EffectBrushTool::strength() const
{
    return std::clamp(m_state->settings(m_tool).opacity, 0.0, 1.0);
}

double EffectBrushTool::falloff(double distance) const
{
    const double t = 1 - distance / radius();
    return t <= 0 ? 0 : t * t * (3 - 2 * t); // smoothstep
}

double EffectBrushTool::allowed(int x, int y) const
{
    return m_mask ? m_mask->at(x, y) / 255.0 : 1.0;
}

std::vector<doc::Pixel> EffectBrushTool::read(const QRect &area) const
{
    std::vector<doc::Pixel> pixels(std::size_t(area.width()) * std::size_t(area.height()));
    m_image->read(area.x(), area.y(), area.width(), area.height(), pixels.data(), area.width());
    return pixels;
}

void EffectBrushTool::write(const QRect &area, const std::vector<doc::Pixel> &pixels)
{
    const doc::Document &d = m_document->document();
    const QRect inside = area.intersected(QRect(0, 0, d.width(), d.height()));
    if (inside.isEmpty())
        return;
    const doc::Pixel *first = pixels.data() + std::ptrdiff_t(inside.y() - area.y()) * area.width()
                              + (inside.x() - area.x());
    m_image->write(inside.x(), inside.y(), inside.width(), inside.height(), first, area.width());
    m_dirty |= inside;
}

void EffectBrushTool::press(const QPointF &docPos, Qt::KeyboardModifiers)
{
    QString reason;
    const auto target = m_document->beginPaint(false, &reason);
    if (!target) {
        m_status(reason.isEmpty() ? m_nothingToDo : reason);
        return;
    }
    m_image = target->image;
    m_before = *m_image;
    m_mask.reset();
    if (m_document->hasSelection())
        m_mask = SelectionMask::fromPath(m_document->selection());
    m_dirty = QRect();
    m_last = docPos;
    m_active = true;
    begin(docPos);
    stamp(docPos, QPointF(0, 0));
    if (!m_dirty.isEmpty())
        m_document->notifyPainted(m_dirty);
}

void EffectBrushTool::stepTo(const QPointF &docPos)
{
    const QPointF delta = docPos - m_last;
    const double length = std::hypot(delta.x(), delta.y());
    const double spacing = std::max(1.0, radius() * 0.15);
    if (length < spacing)
        return; // the rest of the way is kept for the next move
    const QPointF unit = delta / length;
    const int steps = int(length / spacing);
    for (int i = 0; i < steps; ++i) {
        m_last += unit * spacing;
        stamp(m_last, unit * spacing);
    }
}

void EffectBrushTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    m_pen = docPos;
    m_hasPen = true;
    if (!m_active)
        return;
    m_dirty = QRect();
    stepTo(docPos);
    if (!m_dirty.isEmpty())
        m_document->notifyPainted(m_dirty);
}

void EffectBrushTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_active)
        return;
    move(docPos, modifiers);
    m_active = false;
    m_image = nullptr;
    m_document->endPaint(m_before, m_kind);
    m_before = {};
}

void EffectBrushTool::cancel()
{
    if (!m_active)
        return;
    m_active = false;
    *m_image = m_before;
    m_image = nullptr;
    m_before = {};
    const doc::Document &d = m_document->document();
    m_document->notifyPainted(QRect(0, 0, d.width(), d.height()));
    m_document->cancelPaint();
}

bool EffectBrushTool::hover(const QPointF &docPos)
{
    m_pen = docPos;
    m_hasPen = true;
    return true;
}

void EffectBrushTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    if (!m_hasPen)
        return;
    const QPointF center = view.documentToView().map(m_pen);
    const double r = radius() * view.zoom();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 3));
    painter.drawEllipse(center, r, r);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawEllipse(center, r, r);
    painter.restore();
}

// --- blend ----------------------------------------------------------------------------

BlendTool::BlendTool(document::DocumentController *document, tools::ToolState *state,
                     std::function<void(const QString &)> status)
    : EffectBrushTool(document, state, tools::ToolId::Blend, doc::CommandKind::Blend,
                      QObject::tr("Nothing to blend on this frame"), std::move(status))
{
}

void BlendTool::begin(const QPointF &at)
{
    // What the brush carries to begin with: what is under it.
    const int r = int(std::ceil(radius())), size = 2 * r + 1;
    const QRect area(int(std::floor(at.x())) - r, int(std::floor(at.y())) - r, size, size);
    const std::vector<doc::Pixel> pixels = read(area);
    m_carried.assign(pixels.size() * 4, 0.0f);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const Color c = unpack(pixels[i]);
        std::copy(c.begin(), c.end(), m_carried.begin() + std::ptrdiff_t(i * 4));
    }
}

void BlendTool::stamp(const QPointF &at, const QPointF &)
{
    const int r = int(std::ceil(radius())), size = 2 * r + 1;
    const int cx = int(std::floor(at.x())), cy = int(std::floor(at.y()));
    const bool blur = state()->blendSettings().mode == tools::BlendSettings::Mode::Blur;
    const double s = strength();

    if (!blur) {
        // Smudge: the carried color goes onto the pixels, and they in turn flow into the carried one.
        const QRect area(cx - r, cy - r, size, size);
        std::vector<doc::Pixel> pixels = read(area);
        const double pickUp = 0.02 + 0.15 * (1 - s); // the less strength, the sooner it runs dry
        for (int j = 0; j < size; ++j)
            for (int i = 0; i < size; ++i) {
                const double w = falloff(std::hypot(i - r, j - r));
                if (w <= 0)
                    continue;
                const std::size_t k = std::size_t(j) * std::size_t(size) + std::size_t(i);
                const Color old = unpack(pixels[k]);
                Color carried;
                std::copy_n(m_carried.begin() + std::ptrdiff_t(k * 4), 4, carried.begin());
                pixels[k] = pack(mix(old, carried, w * s * allowed(area.x() + i, area.y() + j)));
                const Color next = mix(carried, old, pickUp * w);
                std::copy(next.begin(), next.end(), m_carried.begin() + std::ptrdiff_t(k * 4));
            }
        write(area, pixels);
        return;
    }

    // Blur: toward the average of the neighbourhood (a box as wide as a tenth of the brush).
    const int reach = std::clamp(r / 10 + 1, 1, 8);
    const int wide = size + 2 * reach;
    const QRect area(cx - r - reach, cy - r - reach, wide, wide);
    const std::vector<doc::Pixel> pixels = read(area);
    std::vector<Color> horizontal(pixels.size());
    for (int y = 0; y < wide; ++y)
        for (int x = 0; x < wide; ++x) {
            Color sum{};
            int n = 0;
            for (int dx = -reach; dx <= reach; ++dx) {
                const int sx = x + dx;
                if (sx < 0 || sx >= wide)
                    continue;
                const Color c = unpack(pixels[std::size_t(y) * std::size_t(wide) + std::size_t(sx)]);
                for (std::size_t k = 0; k < 4; ++k)
                    sum[k] += c[k];
                ++n;
            }
            for (float &v : sum)
                v /= float(n);
            horizontal[std::size_t(y) * std::size_t(wide) + std::size_t(x)] = sum;
        }
    std::vector<doc::Pixel> out = pixels;
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            const double w = falloff(std::hypot(i - r, j - r));
            if (w <= 0)
                continue;
            const int x = i + reach, y = j + reach;
            Color sum{};
            int n = 0;
            for (int dy = -reach; dy <= reach; ++dy) {
                const int sy = y + dy;
                if (sy < 0 || sy >= wide)
                    continue;
                const Color &c = horizontal[std::size_t(sy) * std::size_t(wide) + std::size_t(x)];
                for (std::size_t k = 0; k < 4; ++k)
                    sum[k] += c[k];
                ++n;
            }
            for (float &v : sum)
                v /= float(n);
            const std::size_t at4 = std::size_t(y) * std::size_t(wide) + std::size_t(x);
            out[at4] = pack(mix(unpack(pixels[at4]), sum, w * s * allowed(area.x() + x, area.y() + y)));
        }
    write(area, out);
}

// --- liquify --------------------------------------------------------------------------

LiquifyTool::LiquifyTool(document::DocumentController *document, tools::ToolState *state,
                         std::function<void(const QString &)> status)
    : EffectBrushTool(document, state, tools::ToolId::Liquify, doc::CommandKind::Liquify,
                      QObject::tr("Nothing to liquify on this frame"), std::move(status))
{
}

void LiquifyTool::stamp(const QPointF &at, const QPointF &step)
{
    using Mode = tools::LiquifySettings::Mode;
    const Mode mode = state()->liquifySettings().mode;
    const int r = int(std::ceil(radius())), size = 2 * r + 1;
    const int cx = int(std::floor(at.x())), cy = int(std::floor(at.y()));
    // Pixels are taken from a little outside the brush: they move by up to a step, or a fifth of the radius.
    const int margin = int(std::ceil(std::max(std::hypot(step.x(), step.y()), radius() * 0.25))) + 2;
    const int wide = size + 2 * margin;
    const QRect area(cx - r - margin, cy - r - margin, wide, wide);
    const std::vector<doc::Pixel> pixels = read(area);
    std::vector<doc::Pixel> out = pixels;
    const double s = strength();

    auto sample = [&](double x, double y) { // bilinear, in the coordinates of `area`
        x = std::clamp(x - 0.5, 0.0, double(wide - 1));
        y = std::clamp(y - 0.5, 0.0, double(wide - 1));
        const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
        const int x1 = std::min(x0 + 1, wide - 1), y1 = std::min(y0 + 1, wide - 1);
        const double fx = x - x0, fy = y - y0;
        auto at = [&](int px, int py) { return unpack(pixels[std::size_t(py) * std::size_t(wide) + std::size_t(px)]); };
        return mix(mix(at(x0, y0), at(x1, y0), fx), mix(at(x0, y1), at(x1, y1), fx), fy);
    };

    const double centerX = at.x() - area.x(), centerY = at.y() - area.y();
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            const int x = i + margin, y = j + margin;
            const double px = x + 0.5, py = y + 0.5;
            const double w = falloff(std::hypot(px - centerX, py - centerY));
            if (w <= 0)
                continue;
            double fromX = px, fromY = py;
            switch (mode) {
            case Mode::Push: // what was behind the pen's step comes forward
                fromX = px - step.x() * w * s;
                fromY = py - step.y() * w * s;
                break;
            case Mode::Expand: // taken from nearer the center: it looks bigger
                fromX = centerX + (px - centerX) * (1 - 0.2 * w * s);
                fromY = centerY + (py - centerY) * (1 - 0.2 * w * s);
                break;
            case Mode::Shrink:
                fromX = centerX + (px - centerX) * (1 + 0.2 * w * s);
                fromY = centerY + (py - centerY) * (1 + 0.2 * w * s);
                break;
            }
            const std::size_t k = std::size_t(y) * std::size_t(wide) + std::size_t(x);
            out[k] = pack(mix(unpack(pixels[k]), sample(fromX, fromY), allowed(area.x() + x, area.y() + y)));
        }
    write(area, out);
}

} // namespace mothion::canvas
