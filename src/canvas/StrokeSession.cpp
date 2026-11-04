#include "canvas/StrokeSession.h"

#include "canvas/TileQt.h"
#include "core/brush/DabGenerator.h"
#include "core/brush/StrokeShaper.h"

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

// Radii of three box blurs that together approximate a Gaussian falling to
// 1/255 at `radius` ("Gaussian blur with boxes").
std::array<int, 3> gaussBoxes(double radius)
{
    const double sigma = radius / std::sqrt(2.0 * std::log(255.0));
    const double ideal = std::sqrt(12.0 * sigma * sigma / 3.0 + 1.0);
    int wl = int(std::floor(ideal));
    if (wl % 2 == 0)
        --wl;
    wl = std::max(wl, 1);
    const int m = int(std::lround((12.0 * sigma * sigma - 3.0 * wl * wl - 12.0 * wl - 9.0) / (-4.0 * wl - 4.0)));
    std::array<int, 3> boxes{};
    for (int i = 0; i < 3; ++i)
        boxes[std::size_t(i)] = (i < m ? wl - 1 : wl + 1) / 2;
    return boxes;
}

void boxBlur(std::vector<float> &v, int w, int h, int r)
{
    if (r <= 0)
        return;
    std::vector<float> tmp(v.size());
    for (int pass = 0; pass < 2; ++pass) { // horizontal, then vertical
        const int len = pass == 0 ? w : h, lines = pass == 0 ? h : w;
        auto at = [&](std::vector<float> &a, int line, int i) -> float & {
            return pass == 0 ? a[std::size_t(line) * std::size_t(w) + std::size_t(i)]
                             : a[std::size_t(i) * std::size_t(w) + std::size_t(line)];
        };
        for (int line = 0; line < lines; ++line) {
            double sum = 0.0;
            for (int i = -r; i <= r; ++i)
                sum += (i >= 0 && i < len) ? at(v, line, i) : 0.0f;
            for (int i = 0; i < len; ++i) {
                at(tmp, line, i) = float(sum / (2 * r + 1));
                const int add = i + r + 1, sub = i - r;
                if (add < len)
                    sum += at(v, line, add);
                if (sub >= 0)
                    sum -= at(v, line, sub);
            }
        }
        v.swap(tmp);
    }
}

// Cheap per-pixel noise in 0..1, fixed on the canvas.
float noiseAt(int x, int y)
{
    std::uint32_t h = std::uint32_t(x) * 374761393u + std::uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

} // namespace

StrokeSession::StrokeSession(doc::TiledImage &target, std::shared_ptr<const DabStamper> stamper, double opacity,
                             bool erase, const QRect &clip)
    : m_target(target)
    , m_before(target) // pointer copy: shares all tiles
    , m_stamper(std::move(stamper))
    , m_opacity(opacity)
    , m_erase(erase)
    , m_clip(clip)
{
}

StrokeSession::StrokeSession(doc::TiledImage &target, const QColor &color, double opacity, bool erase,
                             const QRect &clip)
    : StrokeSession(target, std::make_shared<DabStamper>(color, 1.0), opacity, erase, clip)
{
}

void StrokeSession::setStyle(const brush::BrushStyle &style)
{
    m_ribbon = style.ribbon;
    m_mix = style.mix;
    m_waterEdge = style.waterEdge;
}

StrokeSession::Paint StrokeSession::mixed(const brush::BrushDab &dab, const QColor &color)
{
    // Average of the paint under the dab (premultiplied, so empty pixels weigh nothing).
    const double reach = std::max(0.5, dab.radius * 0.7);
    constexpr int kGrid = 5;
    double r = 0, g = 0, b = 0, a = 0;
    for (int j = 0; j < kGrid; ++j)
        for (int i = 0; i < kGrid; ++i) {
            const double u = (i - (kGrid - 1) / 2.0) / ((kGrid - 1) / 2.0);
            const double v = (j - (kGrid - 1) / 2.0) / ((kGrid - 1) / 2.0);
            const QRgb p = m_target.pixel(int(std::floor(dab.x + u * reach)), int(std::floor(dab.y + v * reach)));
            r += qRed(p) / 255.0;
            g += qGreen(p) / 255.0;
            b += qBlue(p) / 255.0;
            a += qAlpha(p) / 255.0;
        }
    const double n = kGrid * kGrid;
    double canvasA = a / n;
    QColor canvas = a > 0.0 ? QColor::fromRgbF(float(r / a), float(g / a), float(b / a)) : color;
    if (m_carried) { // color stretch: the brush still holds some of what it picked up
        const double k = std::clamp(m_mix.stretch, 0.0, 1.0);
        auto lerp = [k](double x, double y) { return x + (y - x) * k; };
        canvas = QColor::fromRgbF(float(lerp(canvas.redF(), m_carried->color.redF())),
                                  float(lerp(canvas.greenF(), m_carried->color.greenF())),
                                  float(lerp(canvas.blueF(), m_carried->color.blueF())));
        canvasA = lerp(canvasA, m_carried->alpha);
    }

    const double amount = dab.mixAmount * dab.mixAmount;
    const double wCanvas = (1.0 - amount) * canvasA;
    const double wBrush = amount;
    Paint out;
    if (wCanvas + wBrush <= 1e-6) {
        out.alpha = 0.0; // no paint on the brush and none to pick up
        return out;
    }
    auto blend = [&](double c, double k) { return float((c * wCanvas + k * wBrush) / (wCanvas + wBrush)); };
    out.color = QColor::fromRgbF(blend(canvas.redF(), color.redF()), blend(canvas.greenF(), color.greenF()),
                                 blend(canvas.blueF(), color.blueF()));
    out.alpha = std::clamp((1.0 - dab.mixDensity) * canvasA + dab.mixDensity, 0.0, 1.0);
    m_carried = out;
    return out;
}

QRect StrokeSession::addDabs(std::span<const brush::BrushDab> dabs)
{
    if (dabs.empty())
        return {};

    // What to draw: each dab (or ribbon piece) with its color and bounds.
    struct Item
    {
        brush::BrushDab dab;
        std::optional<brush::BrushDab> from; // ribbon piece start
        QColor color;
        QRectF bounds;
    };
    std::vector<Item> items;
    items.reserve(dabs.size());
    QRectF boundsF;
    for (const brush::BrushDab &d : dabs) {
        Item item{d, std::nullopt, m_stamper->colorFor(d.color), m_stamper->bounds(d)};
        if (m_mix.on && !m_erase) {
            const Paint paint = mixed(d, item.color);
            item.color = paint.color;
            item.dab.alpha *= paint.alpha;
        }
        if (m_ribbon) {
            const std::optional<brush::BrushDab> from = m_previous;
            m_previous = item.dab;
            if (!from)
                continue; // a ribbon starts at its second dab
            item.from = from;
            item.bounds |= m_stamper->bounds(*from);
        }
        boundsF |= item.bounds;
        items.push_back(std::move(item));
    }
    QRect bounds = boundsF.toAlignedRect();
    if (!m_clip.isEmpty())
        bounds &= m_clip;
    if (bounds.isEmpty())
        return {};

    for (const doc::TileCoord &c : tilesIn(bounds)) {
        const QRectF tileArea = tileRect(c);
        // Stamp the dabs touching this tile on the mask, in document coordinates.
        bool any = false;
        for (const Item &item : items)
            any = any || item.bounds.intersects(tileArea);
        if (!any)
            continue;
        QImage mask = tileImage(m_mask.writableTile(c));
        {
            QPainter p(&mask);
            p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
            p.translate(-c.x * doc::kTileSize, -c.y * doc::kTileSize);
            if (!m_clip.isEmpty())
                p.setClipRect(m_clip);
            for (const Item &item : items) {
                if (!item.bounds.intersects(tileArea))
                    continue;
                if (item.from)
                    m_stamper->drawRibbon(p, *item.from, item.dab, item.color);
                else
                    m_stamper->draw(p, item.dab, item.color);
            }
        }
        m_touched.insert(c);
        recomposeTile(c);
    }
    return bounds;
}

QRect StrokeSession::addCornerTip(const brush::CornerTip &tip, const brush::BrushDab &dab)
{
    if (dab.alpha <= 0.0)
        return {};
    QPolygonF quad;
    for (int k = 0; k < 4; ++k)
        quad << QPointF(tip.x[k], tip.y[k]);
    QRect bounds = quad.boundingRect().toAlignedRect().adjusted(-1, -1, 1, 1);
    if (!m_clip.isEmpty())
        bounds &= m_clip;
    if (bounds.isEmpty())
        return {};
    const QColor color = m_stamper->colorFor(dab.color);
    for (const doc::TileCoord &c : tilesIn(bounds)) {
        QImage mask = tileImage(m_mask.writableTile(c));
        {
            QPainter p(&mask);
            p.setRenderHint(QPainter::Antialiasing);
            p.translate(-c.x * doc::kTileSize, -c.y * doc::kTileSize);
            if (!m_clip.isEmpty())
                p.setClipRect(m_clip);
            p.setOpacity(std::clamp(dab.alpha, 0.0, 1.0));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(color.rgb()));
            p.drawPolygon(quad);
        }
        m_touched.insert(c);
        recomposeTile(c);
    }
    return bounds;
}

QRect StrokeSession::paintStroke(const brush::BrushStyle &style, std::uint32_t seed,
                                 std::vector<brush::BrushInput> points)
{
    setStyle(style);
    points = brush::finalPoints(style, std::move(points));
    brush::DabGenerator generator(style, seed);
    generator.setStrokeLength(brush::strokeLength(points));
    return drawPoints(generator, points) | finish();
}

QRect StrokeSession::drawPoints(brush::DabGenerator &generator, const std::vector<brush::BrushInput> &points,
                                bool cornerTips)
{
    QRect changed;
    for (std::size_t i = 0; i < points.size(); ++i) {
        changed |= addDabs(i == 0 ? generator.begin(points[i]) : generator.continueTo(points[i]));
        if (!cornerTips || !points[i].corner)
            continue;
        const brush::BrushDab dab = generator.dabHere(points[i]);
        if (const auto tip = brush::cornerTipAt(points, i, dab.radius))
            changed |= addCornerTip(*tip, dab);
    }
    return changed;
}

void StrokeSession::recomposeTile(doc::TileCoord c)
{
    const doc::TilePtr maskTile = m_mask.tile(c);
    const doc::TilePtr beforeTile = m_before.tile(c);
    if (!maskTile) { // nothing of the stroke here (fully clipped)
        m_target.setTile(c, beforeTile);
        return;
    }
    // writableTile() clones the shared "before" tile, or creates a fresh one.
    QImage out = tileImage(m_target.writableTile(c));
    {
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        if (beforeTile)
            p.drawImage(0, 0, tileImage(*beforeTile));
        else
            p.fillRect(out.rect(), Qt::transparent);
        p.setCompositionMode(m_erase ? QPainter::CompositionMode_DestinationOut
                                     : QPainter::CompositionMode_SourceOver);
        p.setOpacity(m_opacity);
        p.drawImage(0, 0, tileImage(*maskTile));
    }
    if (m_target.tile(c)->isTransparent()) // e.g. erased to nothing
        m_target.setTile(c, nullptr);
}

QRect StrokeSession::touchedArea() const
{
    QRect area;
    for (const doc::TileCoord &c : m_touched)
        area |= tileRect(c);
    return area;
}

QRect StrokeSession::restart()
{
    const QRect area = touchedArea();
    for (const doc::TileCoord &c : m_touched)
        m_target.setTile(c, m_before.tile(c));
    m_touched.clear();
    m_mask = {};
    m_previous.reset();
    m_carried.reset();
    return area;
}

QRect StrokeSession::finish()
{
    if (!m_waterEdge.on || m_erase || m_touched.empty())
        return {};
    const double radius = std::max(1.0, m_waterEdge.radius);
    const int pad = int(std::ceil(radius + m_waterEdge.blur)) + 1;
    QRect area = touchedArea().adjusted(-pad, -pad, pad, pad);
    if (!m_clip.isEmpty())
        area &= m_clip;
    if (area.isEmpty())
        return {};

    QImage img = toQImage(m_mask, area);
    const int w = img.width(), h = img.height();
    auto idx = [w](int x, int y) { return std::size_t(y) * std::size_t(w) + std::size_t(x); };

    // Distance of each painted pixel to the outside of the stroke (chamfer 1 / √2).
    constexpr float kFar = 1e9f;
    std::vector<float> dist(std::size_t(w) * std::size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            dist[idx(x, y)] = qAlpha(img.pixel(x, y)) > 0 ? kFar : 0.0f;
    const float diag = float(std::numbers::sqrt2);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float &d = dist[idx(x, y)];
            if (x > 0) d = std::min(d, dist[idx(x - 1, y)] + 1.0f);
            if (y > 0) d = std::min(d, dist[idx(x, y - 1)] + 1.0f);
            if (x > 0 && y > 0) d = std::min(d, dist[idx(x - 1, y - 1)] + diag);
            if (x + 1 < w && y > 0) d = std::min(d, dist[idx(x + 1, y - 1)] + diag);
        }
    for (int y = h - 1; y >= 0; --y)
        for (int x = w - 1; x >= 0; --x) {
            float &d = dist[idx(x, y)];
            if (x + 1 < w) d = std::min(d, dist[idx(x + 1, y)] + 1.0f);
            if (y + 1 < h) d = std::min(d, dist[idx(x, y + 1)] + 1.0f);
            if (x + 1 < w && y + 1 < h) d = std::min(d, dist[idx(x + 1, y + 1)] + diag);
            if (x > 0 && y + 1 < h) d = std::min(d, dist[idx(x - 1, y + 1)] + diag);
        }

    // Rim strength: full on the outermost pixel, fading to 0 at `radius`,
    // weighted by the coverage there and broken up by noise.
    std::vector<float> rim(dist.size(), 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float d = dist[idx(x, y)];
            if (d <= 0.0f || d > radius)
                continue;
            const double base = d <= 1.0f ? 1.0 : (radius - d) / radius;
            const double k = qAlpha(img.pixel(x, y)) / 255.0;
            rim[idx(x, y)] = float(k * base * (0.6 + 0.8 * noiseAt(area.x() + x, area.y() + y)));
        }
    if (m_waterEdge.blur > 0.0)
        for (int r : gaussBoxes(m_waterEdge.blur))
            boxBlur(rim, w, h, r);

    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float e = std::min(1.0f, rim[idx(x, y)]);
            const int a = qAlpha(line[x]);
            if (e <= 0.0f || a == 0)
                continue;
            const double na = std::min(1.0, a / 255.0 + m_waterEdge.alphaPower * e);
            const double darken = 1.0 - std::clamp(m_waterEdge.valuePower, 0.0, 1.0) * e;
            // Unpremultiply, darken, premultiply at the new alpha.
            auto channel = [&](int c) { return int(std::lround(std::min(255.0, c * 255.0 / a) * darken * na)); };
            line[x] = qRgba(channel(qRed(line[x])), channel(qGreen(line[x])), channel(qBlue(line[x])),
                            int(std::lround(na * 255.0)));
        }
    }

    // Back into the mask, then recompose every tile of the area.
    for (const doc::TileCoord &c : tilesIn(area)) {
        const QRect tile = tileRect(c) & area;
        bool any = false;
        for (int y = tile.top(); y <= tile.bottom() && !any; ++y)
            for (int x = tile.left(); x <= tile.right() && !any; ++x)
                any = qAlpha(img.pixel(x - area.x(), y - area.y())) > 0;
        if (!any && !m_mask.tile(c))
            continue;
        QImage maskTile = tileImage(m_mask.writableTile(c));
        QPainter p(&maskTile);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.drawImage(tile.topLeft() - tileRect(c).topLeft(), img, tile.translated(-area.topLeft()));
        p.end();
        m_touched.insert(c);
        recomposeTile(c);
    }
    return area;
}

void StrokeSession::cancel()
{
    restart();
}

} // namespace mothion::canvas
