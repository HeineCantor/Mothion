#include "canvas/ShapeTool.h"

#include "canvas/SelectionMask.h"
#include "canvas/VectorRender.h"
#include "canvas/ViewTransform.h"
#include "core/document/Tile.h"
#include "document/DocumentController.h"

#include <QGuiApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace mothion::canvas {

using tools::ShapeSettings;

namespace {

constexpr double kClickTolerance = 1.0; // document px: a shorter drag draws nothing
constexpr double kAngleStep = 15.0;     // degrees, a line with Shift
constexpr double kCloseDistance = 8.0;  // screen px: a click this near the first point closes a polygon
constexpr double kDoubleClick = 3.0;    // screen px: a click this near the last point ends the shape

double signOf(double v)
{
    return v < 0 ? -1.0 : 1.0;
}

// `color` at `factor` (0..255) over `pixel`, premultiplied; `erase` removes
// what is under the shape instead of covering it.
doc::Pixel composite(doc::Pixel pixel, doc::Pixel shape, int factor, bool erase)
{
    const int a = (int((shape >> 24) & 0xff) * factor + 127) / 255;
    doc::Pixel out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int s = erase ? 0 : (int((shape >> shift) & 0xff) * factor + 127) / 255;
        const int p = int((pixel >> shift) & 0xff);
        out |= doc::Pixel(s + (p * (255 - a) + 127) / 255) << shift;
    }
    return out;
}

} // namespace

ShapeGeometry shapeGeometry(ShapeSettings::Kind kind, const QPointF &start, const QPointF &end,
                            Qt::KeyboardModifiers modifiers)
{
    const bool shift = modifiers & Qt::ShiftModifier;
    const bool fromCenter = modifiers & Qt::AltModifier;
    double dx = end.x() - start.x(), dy = end.y() - start.y();
    ShapeGeometry g;
    if (kind == ShapeSettings::Kind::Line || kind == ShapeSettings::Kind::Curve) {
        if (shift) {
            const double length = std::hypot(dx, dy);
            const double step = kAngleStep * std::numbers::pi / 180.0;
            const double angle = std::round(std::atan2(dy, dx) / step) * step;
            dx = length * std::cos(angle);
            dy = length * std::sin(angle);
        }
        g.line = fromCenter ? QLineF(start - QPointF(dx, dy), start + QPointF(dx, dy))
                            : QLineF(start, start + QPointF(dx, dy));
        return g;
    }
    if (shift) {
        const double side = std::max(std::abs(dx), std::abs(dy));
        dx = signOf(dx) * side;
        dy = signOf(dy) * side;
    }
    const QPointF corner = start + QPointF(dx, dy);
    g.box = (fromCenter ? QRectF(start - QPointF(dx, dy), corner) : QRectF(start, corner)).normalized();
    // Whole pixels, so a rectangle's edges do not blur across two of them.
    g.box = QRectF(QPointF(std::round(g.box.left()), std::round(g.box.top())),
                   QPointF(std::round(g.box.right()), std::round(g.box.bottom())));
    return g;
}

QPainterPath shapePath(const ShapeSettings &settings, const ShapeGeometry &g)
{
    QPainterPath path;
    switch (settings.kind) {
    case ShapeSettings::Kind::Line:
        path.moveTo(g.line.p1());
        path.lineTo(g.line.p2());
        break;
    case ShapeSettings::Kind::Rectangle: path.addRect(g.box); break;
    case ShapeSettings::Kind::RoundedRectangle: {
        const double r = std::min(double(settings.cornerRadius), std::min(g.box.width(), g.box.height()) / 2);
        path.addRoundedRect(g.box, r, r);
        break;
    }
    case ShapeSettings::Kind::Ellipse: path.addEllipse(g.box); break;
    case ShapeSettings::Kind::Polygon:
        if (!g.points.isEmpty()) {
            path.addPolygon(g.points);
            path.closeSubpath();
        }
        break;
    case ShapeSettings::Kind::Curve: {
        const QPolygonF &pts = g.points;
        if (pts.isEmpty())
            break;
        path.moveTo(pts.first());
        // Catmull-Rom: each piece is a cubic whose handles follow the neighbours.
        for (qsizetype i = 0; i + 1 < pts.size(); ++i) {
            const QPointF p0 = pts[i > 0 ? i - 1 : i], p1 = pts[i], p2 = pts[i + 1],
                          p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
            path.cubicTo(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
        }
        break;
    }
    }
    return path;
}

RenderedShape renderShape(const ShapeSettings &settings, double thickness, const QColor &main,
                          const QColor &secondary, const ShapeGeometry &geometry, const QRect &clip)
{
    const bool open = ShapeSettings::isOpen(settings.kind);
    const bool outline = open || settings.style != ShapeSettings::Style::Filled;
    const bool filled = !open && settings.style != ShapeSettings::Style::Outline;
    const double reach = outline ? thickness : 0.0; // a mitered corner reaches past half the thickness
    const QPainterPath path = shapePath(settings, geometry);
    const QRect area =
        path.controlPointRect().adjusted(-reach, -reach, reach, reach).toAlignedRect().adjusted(-1, -1, 1, 1) & clip;
    if (path.isEmpty() || area.isEmpty())
        return {};

    RenderedShape out;
    out.origin = area.topLeft();
    out.image = QImage(area.size(), QImage::Format_ARGB32_Premultiplied);
    out.image.fill(Qt::transparent);
    QPainter p(&out.image);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(-QPointF(area.topLeft()));
    if (outline)
        p.setPen(QPen(main, thickness, Qt::SolidLine, open ? Qt::RoundCap : Qt::SquareCap,
                      open ? Qt::RoundJoin : Qt::MiterJoin));
    else
        p.setPen(Qt::NoPen);
    p.setBrush(filled ? QBrush(outline ? secondary : main) : QBrush(Qt::NoBrush));
    p.drawPath(path);
    return out;
}

ShapeTool::ShapeTool(document::DocumentController *document, tools::ToolState *state,
                     std::function<void(const QString &)> status, std::function<void()> changed)
    : m_document(document)
    , m_state(state)
    , m_status(std::move(status))
    , m_changed(std::move(changed))
{
}

void ShapeTool::cancel()
{
    m_active = false;
    m_points.clear();
    notify();
}

QRectF ShapeTool::pendingBounds() const
{
    if (!building())
        return {};
    return m_points.boundingRect().adjusted(-0.5, -0.5, 0.5, 0.5); // never empty, even for one point
}

bool ShapeTool::canFinish() const
{
    const bool polygon = m_state->shapeSettings().kind == ShapeSettings::Kind::Polygon;
    return building() && m_points.size() >= (polygon ? 3 : 2);
}

bool ShapeTool::building() const
{
    return !m_points.isEmpty() && ShapeSettings::isMultiPoint(m_state->shapeSettings().kind);
}

std::vector<doc::VectorStroke> shapeStrokes(const tools::ShapeSettings &settings, double thickness, double opacity,
                                            const QColor &color, bool erase, const ShapeGeometry &geometry)
{
    constexpr double kCornerAngle = 25.0; // degrees: a turn sharper than this is a corner
    std::vector<doc::VectorStroke> strokes;
    const QPainterPath path = shapePath(settings, geometry);
    for (const QPolygonF &polygon : path.toSubpathPolygons(QTransform())) {
        std::vector<QPointF> at;
        for (const QPointF &p : polygon)
            if (at.empty() || QLineF(at.back(), p).length() > 1e-6)
                at.push_back(p);
        if (at.size() < 2)
            continue;
        const bool closed = at.size() > 2 && QLineF(at.front(), at.back()).length() < 1e-6;
        if (closed)
            at.back() = at.front();
        auto heading = [&](std::size_t from, std::size_t to) {
            return std::atan2(at[to].y() - at[from].y(), at[to].x() - at[from].x()) * 180.0 / std::numbers::pi;
        };
        auto turn = [](double a, double b) {
            double d = std::fmod(std::abs(a - b), 360.0);
            return d > 180.0 ? 360.0 - d : d;
        };
        doc::VectorStroke stroke;
        stroke.size = thickness;
        stroke.opacity = opacity;
        stroke.color = color.rgba();
        stroke.erase = erase;
        stroke.cornerTips = true;
        const std::size_t last = at.size() - 1;
        for (std::size_t i = 0; i <= last; ++i) {
            brush::BrushInput in;
            in.x = at[i].x();
            in.y = at[i].y();
            in.hasTilt = false;
            const double before = i > 0 ? heading(i - 1, i) : closed ? heading(last - 1, last) : heading(0, 1);
            const double after = i < last ? heading(i, i + 1) : closed ? heading(0, 1) : before;
            in.direction = i < last ? after : before;
            in.corner = (i > 0 || closed) && (i < last || closed) && turn(before, after) > kCornerAngle;
            stroke.points.push_back(in);
        }
        strokes.push_back(std::move(stroke));
    }
    return strokes;
}

QPointF ShapeTool::constrained(const QPointF &docPos, Qt::KeyboardModifiers modifiers) const
{
    // Shift: the next point on a 15° line from the last one.
    if (m_points.isEmpty() || !(modifiers & Qt::ShiftModifier))
        return docPos;
    return shapeGeometry(ShapeSettings::Kind::Line, m_points.last(), docPos, Qt::ShiftModifier).line.p2();
}

void ShapeTool::press(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (ShapeSettings::isMultiPoint(m_state->shapeSettings().kind)) {
        // A click adds a point; the release does nothing, and the shape goes on.
        const QPointF at = constrained(docPos, modifiers);
        if (!building()) {
            m_points = {at};
        } else {
            const bool polygon = m_state->shapeSettings().kind == ShapeSettings::Kind::Polygon;
            const double zoom = std::max(m_zoom, 1e-6);
            if (polygon && m_points.size() >= 3 && QLineF(m_points.first(), at).length() * zoom < kCloseDistance)
                return commit(); // back on the first point
            if (QLineF(m_points.last(), at).length() * zoom < kDoubleClick)
                return commit(); // a double click
            m_points << at;
        }
        m_hover = at;
        notify();
        return;
    }
    m_points.clear();
    m_active = true;
    m_start = m_current = docPos;
    m_modifiers = modifiers;
}

void ShapeTool::move(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_active)
        return;
    m_current = docPos;
    m_modifiers = modifiers;
}

bool ShapeTool::hover(const QPointF &docPos)
{
    if (!building())
        return false;
    m_hover = constrained(docPos, QGuiApplication::keyboardModifiers());
    return true;
}

ShapeGeometry ShapeTool::currentGeometry(bool withHover) const
{
    const ShapeSettings settings = m_state->shapeSettings();
    if (ShapeSettings::isMultiPoint(settings.kind)) {
        ShapeGeometry g;
        g.points = m_points;
        if (withHover && !m_points.isEmpty() && QLineF(m_points.last(), m_hover).length() > 0)
            g.points << m_hover;
        return g;
    }
    return shapeGeometry(settings.kind, m_start, m_current, m_modifiers);
}

RenderedShape ShapeTool::render(const ShapeGeometry &geometry) const
{
    const doc::Document &d = m_document->document();
    return renderShape(m_state->shapeSettings(), m_state->settings(tools::ToolId::Shape).size, m_state->color(),
                       m_state->inactiveColor(), geometry, QRect(0, 0, d.width(), d.height()));
}

void ShapeTool::release(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_active)
        return;
    move(docPos, modifiers);
    m_active = false;
    if (QLineF(m_start, m_current).length() < kClickTolerance)
        return;
    draw(currentGeometry(false));
}

void ShapeTool::commit()
{
    if (!building())
        return;
    const bool enough = canFinish();
    const ShapeGeometry geometry = currentGeometry(false);
    m_points.clear();
    notify();
    if (enough)
        draw(geometry);
}

bool ShapeTool::wantsKey(int key) const
{
    return building()
           && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape || key == Qt::Key_Backspace);
}

bool ShapeTool::keyPress(int key)
{
    if (!building())
        return false;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter: commit(); return true;
    case Qt::Key_Escape: cancel(); return true;
    case Qt::Key_Backspace:
        m_points.removeLast();
        notify();
        return true;
    default: return false;
    }
}

void ShapeTool::draw(const ShapeGeometry &geometry)
{
    const RenderedShape shape = render(geometry);
    if (shape.image.isNull())
        return; // all of it outside the page

    QString reason;
    const auto target = m_document->beginPaint(true, &reason, true, true); // a vector layer keeps the outline
    if (!target) {
        if (!reason.isEmpty())
            m_status(reason);
        return;
    }
    const doc::TiledImage before = *target->image;

    if (target->vector) { // the outline kept as strokes, which are drawn from there
        const tools::ShapeSettings settings = m_state->shapeSettings();
        if (!ShapeSettings::isOpen(settings.kind) && settings.style != ShapeSettings::Style::Outline)
            m_status(QObject::tr("A vector layer has no fill: only the outline is drawn"));
        const tools::BrushSettings brush = m_state->settings(tools::ToolId::Shape);
        std::vector<doc::VectorStroke> strokes = shapeStrokes(settings, brush.size, brush.opacity, m_state->color(),
                                                              m_state->transparentSelected(), geometry);
        if (m_document->hasSelection()) { // only what is inside the selection is kept
            const SelectionMask mask = SelectionMask::fromPath(m_document->selection());
            std::vector<doc::VectorStroke> inside;
            for (const doc::VectorStroke &stroke : strokes) {
                SplitStroke cut = splitStrokeByMask(stroke, mask);
                inside.insert(inside.end(), cut.inside.begin(), cut.inside.end());
            }
            strokes = std::move(inside);
        }
        const doc::Document &d = m_document->document();
        const QRect painted = drawStrokes(*target->image, strokes, *m_state, QRect(0, 0, d.width(), d.height()));
        for (doc::VectorStroke &stroke : strokes)
            m_document->attachStroke(std::move(stroke));
        m_document->notifyPainted(painted);
        m_document->endPaint(before, doc::CommandKind::Shape);
        return;
    }

    const QRect box(shape.origin, shape.image.size());
    const SelectionMask selection = SelectionMask::fromPath(m_document->selection());
    const bool selected = m_document->hasSelection();
    const bool erase = m_state->transparentSelected();
    const int opacity = int(std::lround(255 * m_state->settings(tools::ToolId::Shape).opacity));
    std::vector<doc::Pixel> pixels(std::size_t(box.width()) * std::size_t(box.height()));
    target->image->read(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    for (int y = 0; y < box.height(); ++y) {
        const auto *line = reinterpret_cast<const doc::Pixel *>(shape.image.constScanLine(y));
        for (int x = 0; x < box.width(); ++x) {
            if (!(line[x] >> 24))
                continue;
            int factor = opacity;
            if (selected)
                factor = (factor * selection.at(box.x() + x, box.y() + y) + 127) / 255;
            doc::Pixel &p = pixels[std::size_t(y) * std::size_t(box.width()) + std::size_t(x)];
            p = composite(p, line[x], factor, erase != target->mask); // on a mask a shape shows the layer: it erases
        }
    }
    target->image->write(box.x(), box.y(), box.width(), box.height(), pixels.data(), box.width());
    m_document->notifyPainted(box);
    m_document->endPaint(before, doc::CommandKind::Shape);
}

void ShapeTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    if (!m_active && !building())
        return;
    const RenderedShape shape = render(currentGeometry(true));
    if (!shape.image.isNull()) {
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, view.zoom() < 2.0);
        if (m_document->hasSelection())
            painter.setClipPath(view.documentToView().map(m_document->selection()));
        painter.setTransform(view.documentToView());
        painter.setOpacity(std::max(0.25, m_state->settings(tools::ToolId::Shape).opacity));
        painter.drawImage(shape.origin, shape.image);
        painter.restore();
    }
    if (building()) { // the points placed, the first one larger: where a polygon closes
        painter.setBrush(Qt::white);
        painter.setPen(QPen(Qt::black, 1));
        for (qsizetype i = 0; i < m_points.size(); ++i) {
            const double half = i == 0 ? 4.5 : 3.0;
            const QPointF at = view.documentToView().map(m_points[i]);
            painter.drawRect(QRectF(at.x() - half, at.y() - half, 2 * half, 2 * half));
        }
    }
}

} // namespace mothion::canvas
