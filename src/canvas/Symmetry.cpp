#include "canvas/Symmetry.h"

#include <cmath>
#include <numbers>

namespace mothion::canvas {

namespace {

using Kind = tools::SymmetrySettings::Kind;

// Into the axes' own frame (the center at the origin, the angle taken away) and back.
QTransform toAxes(const QPointF &center, double angle)
{
    return QTransform::fromTranslate(-center.x(), -center.y()) * QTransform().rotate(-angle);
}

const QTransform kMirrorX(-1, 0, 0, 1, 0, 0); // left and right swapped
const QTransform kMirrorY(1, 0, 0, -1, 0, 0); // up and down swapped

int floorDiv(double value, double step) { return int(std::floor(value / step)); }
// The last cell (of `step`, from `origin`) that starts before `end`.
int lastCell(double end, double origin, double step) { return int(std::ceil((end - origin) / step)) - 1; }

// Where `kind` takes the column `column`: every other one half a cell down for HalfDrop.
double dropOf(Kind kind, int column, double cellHeight)
{
    return kind == Kind::HalfDrop && ((column % 2) + 2) % 2 == 1 ? cellHeight / 2.0 : 0.0;
}

// A way given as an angle (degrees), sent through the linear part of `t`.
double turned(double degrees, const QTransform &t)
{
    const double a = degrees * std::numbers::pi / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    return std::atan2(t.m12() * c + t.m22() * s, t.m11() * c + t.m21() * s) * 180.0 / std::numbers::pi;
}

} // namespace

bool symmetryTurns(Kind kind) { return kind != Kind::Tile && kind != Kind::HalfDrop; }

std::vector<QTransform> symmetryTransforms(const tools::SymmetrySettings &settings, const QPointF &center,
                                           double angle, const QSizeF &page, const QPointF &start)
{
    std::vector<QTransform> out;
    const Kind kind = settings.kind;
    if (kind == Kind::Tile || kind == Kind::HalfDrop) {
        const int n = settings.tiles;
        const double cw = page.width() / n, ch = page.height() / n;
        if (cw <= 0 || ch <= 0)
            return {QTransform()};
        const int ci = floorDiv(start.x() - center.x(), cw);
        const int cj = floorDiv(start.y() - center.y() - dropOf(kind, ci, ch), ch);
        out.push_back(QTransform());
        // Every cell that reaches into the page (one more row for the columns a half drop moves down).
        const int firstColumn = floorDiv(-center.x(), cw), lastColumn = lastCell(page.width(), center.x(), cw);
        const int firstRow = floorDiv(-center.y(), ch) - (kind == Kind::HalfDrop ? 1 : 0);
        const int lastRow = lastCell(page.height(), center.y(), ch);
        for (int i = firstColumn; i <= lastColumn; ++i)
            for (int j = firstRow; j <= lastRow; ++j) {
                if (i == ci && j == cj)
                    continue;
                const double dx = (i - ci) * cw;
                const double dy = (j - cj) * ch + dropOf(kind, i, ch) - dropOf(kind, ci, ch);
                out.push_back(QTransform::fromTranslate(dx, dy));
            }
        return out;
    }

    std::vector<QTransform> local;
    switch (kind) {
    case Kind::Bilateral: local = {QTransform(), kMirrorX}; break;
    case Kind::Vertical: local = {QTransform(), kMirrorY}; break;
    case Kind::Both: local = {QTransform(), kMirrorX, kMirrorY, kMirrorX * kMirrorY}; break;
    case Kind::Rotate:
    case Kind::RotateMirror: {
        const int n = settings.count;
        for (int k = 0; k < n; ++k)
            local.push_back(QTransform().rotate(360.0 * k / n));
        if (kind == Kind::RotateMirror)
            for (int k = 0; k < n; ++k)
                local.push_back(kMirrorX * QTransform().rotate(360.0 * k / n));
        break;
    }
    case Kind::Tile:
    case Kind::HalfDrop: break;
    }
    const QTransform into = toAxes(center, angle), back = into.inverted();
    for (const QTransform &m : local)
        out.push_back(m.isIdentity() ? QTransform() : into * m * back);
    return out;
}

brush::BrushInput transformedInput(const brush::BrushInput &in, const QTransform &transform)
{
    brush::BrushInput out = in;
    const QPointF at = transform.map(QPointF(in.x, in.y));
    out.x = at.x();
    out.y = at.y();
    out.direction = turned(in.direction, transform);
    out.penAngle = turned(in.penAngle, transform);
    return out;
}

std::vector<QLineF> symmetryGuides(const tools::SymmetrySettings &settings, const QPointF &center, double angle,
                                   const QSizeF &page, double reach)
{
    std::vector<QLineF> out;
    const Kind kind = settings.kind;
    if (kind == Kind::Tile || kind == Kind::HalfDrop) {
        const int n = settings.tiles;
        const double cw = page.width() / n, ch = page.height() / n;
        if (cw <= 0 || ch <= 0)
            return out;
        const int firstColumn = floorDiv(-center.x(), cw), lastColumn = lastCell(page.width(), center.x(), cw);
        for (int i = firstColumn; i <= lastColumn + 1; ++i) {
            const double x = center.x() + i * cw;
            if (x > 0 && x < page.width())
                out.emplace_back(x, 0.0, x, page.height());
        }
        // The rows: across the page for Tile; column by column for HalfDrop, every other one moved down.
        const int firstRow = floorDiv(-center.y(), ch) - 1, lastRow = floorDiv(page.height() - center.y(), ch) + 1;
        auto rows = [&](double left, double right, double drop) {
            for (int j = firstRow; j <= lastRow; ++j) {
                const double y = center.y() + j * ch + drop;
                if (y > 0 && y < page.height())
                    out.emplace_back(left, y, right, y);
            }
        };
        if (kind == Kind::Tile) {
            rows(0.0, page.width(), 0.0);
            return out;
        }
        for (int i = firstColumn; i <= lastColumn; ++i)
            rows(std::max(0.0, center.x() + i * cw), std::min(page.width(), center.x() + (i + 1) * cw),
                 dropOf(kind, i, ch));
        return out;
    }
    const QTransform back = toAxes(center, angle).inverted();
    auto line = [&](double degreesFromUp, bool wholeLine) {
        // A way in the axes' frame: 0 is up.
        const double a = degreesFromUp * std::numbers::pi / 180.0;
        const QPointF way(std::sin(a) * reach, -std::cos(a) * reach);
        out.emplace_back(back.map(wholeLine ? -way : QPointF()), back.map(way));
    };
    switch (kind) {
    case Kind::Bilateral: line(0, true); break;
    case Kind::Vertical: line(90, true); break;
    case Kind::Both:
        line(0, true);
        line(90, true);
        break;
    case Kind::Rotate:
        for (int k = 0; k < settings.count; ++k)
            line(360.0 * k / settings.count, false); // a ray for each copy
        break;
    case Kind::RotateMirror:
        for (int k = 0; k < settings.count; ++k)
            line(180.0 * k / settings.count, true); // the mirrors
        break;
    case Kind::Tile:
    case Kind::HalfDrop: break;
    }
    return out;
}

} // namespace mothion::canvas
