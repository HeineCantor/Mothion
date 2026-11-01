#include "canvas/PuppetTool.h"

#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

#include <QPainter>
#include <QPalette>
#include <QApplication>

namespace mothion::canvas {

namespace {

constexpr double kPinRadius = 5.0;   // screen px, as drawn
constexpr double kGrabRadius = 10.0; // screen px, to pick a pin
constexpr int kDragIterations = 8;   // ARAP steps per pen move
constexpr int kSettleIterations = 40; // when the pen lifts

puppet::RigSettings rigSettings(const tools::PuppetSettings &s) { return {s.density, s.expand}; }

QRect boundsOf(const doc::TiledImage &image)
{
    QRect r;
    for (const doc::TileCoord &c : image.tileCoords())
        r |= tileRect(c);
    return r;
}

} // namespace

PuppetTool::PuppetTool(document::DocumentController *document, tools::ToolState *toolState,
                       std::function<void(const QString &)> status, std::function<void()> repaint)
    : m_document(document)
    , m_toolState(toolState)
    , m_status(std::move(status))
    , m_repaint(std::move(repaint))
{
    m_settingsConnection = QObject::connect(m_toolState, &tools::ToolState::puppetSettingsChanged, m_toolState,
                                            [this] { settingsChanged(); });
}

PuppetTool::~PuppetTool()
{
    QObject::disconnect(m_settingsConnection);
}

bool PuppetTool::start()
{
    QString reason;
    const auto target = m_document->beginPaint(false, &reason);
    if (!target) {
        m_status(reason.isEmpty() ? QObject::tr("Nothing to warp on this frame") : reason);
        return false;
    }
    auto s = std::make_unique<Session>();
    s->image = target->image;
    s->before = *target->image;
    doc::TiledImage warped;
    if (m_document->hasSelection())
        splitByMask(s->before, SelectionMask::fromPath(m_document->selection()), warped, s->rest);
    else
        warped = s->before;

    // The pixels to warp, flat, and the shape they draw.
    const QRect area = boundsOf(warped);
    if (area.isEmpty()) {
        m_document->cancelPaint();
        m_status(QObject::tr("Nothing to warp here"));
        return false;
    }
    s->source = puppet::RasterImage(area.x(), area.y(), area.width(), area.height());
    warped.read(area.x(), area.y(), area.width(), area.height(), s->source.pixels.data(), area.width());
    puppet::Bitmask shape(area.x(), area.y(), area.width(), area.height());
    for (std::size_t i = 0; i < s->source.pixels.size(); ++i)
        shape.bits[i] = (s->source.pixels[i] >> 24) != 0 ? 1 : 0;

    s->rig = std::make_unique<puppet::PuppetRig>(shape, rigSettings(m_toolState->puppetSettings()));
    if (s->rig->isEmpty()) {
        m_document->cancelPaint();
        m_status(QObject::tr("Nothing to warp here"));
        return false;
    }
    s->shown = area;
    m_session = std::move(s);
    m_selectedPin.reset();
    return true;
}

void PuppetTool::render()
{
    Session &s = *m_session;
    int x, y, w, h;
    puppet::deformedBounds(*s.rig, x, y, w, h);
    puppet::RasterImage target(x, y, w, h);
    puppet::warp(*s.rig, s.source, target);
    doc::TiledImage result = s.rest;
    doc::TiledImage warped;
    warped.write(x, y, w, h, target.pixels.data(), w);
    compositeOver(result, warped);
    *s.image = std::move(result);
    const QRect now(x, y, w, h);
    m_document->notifyPainted(s.shown | now);
    s.shown = now;
    s.changed = true;
}

void PuppetTool::press(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    if (!m_session && !start())
        return;
    puppet::PuppetRig &rig = *m_session->rig;
    const puppet::Vec2 at{docPos.x(), docPos.y()};
    if (const auto pin = rig.pinAt(at, kGrabRadius / m_zoom)) {
        if (modifiers & Qt::AltModifier) {
            rig.removePin(*pin);
            m_selectedPin.reset();
            if (m_session->changed)
                render();
            m_repaint();
            return;
        }
        m_dragPin = m_selectedPin = *pin;
        m_grabOffset = rig.pins()[std::size_t(*pin)].position - at;
    } else if (const auto added = rig.addPin(at)) {
        m_dragPin = m_selectedPin = *added;
        m_grabOffset = {};
    } else {
        m_status(QObject::tr("Click on the drawing to place a pin"));
    }
    m_repaint();
}

void PuppetTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_session || !m_dragPin)
        return;
    m_session->rig->movePin(*m_dragPin, puppet::Vec2{docPos.x(), docPos.y()} + m_grabOffset, kDragIterations);
    m_dragMoved = true;
    render();
}

void PuppetTool::release(const QPointF &docPos, Qt::KeyboardModifiers)
{
    if (!m_session || !m_dragPin)
        return;
    if (m_dragMoved) { // settle the shape where the pen left it
        m_session->rig->movePin(*m_dragPin, puppet::Vec2{docPos.x(), docPos.y()} + m_grabOffset, kSettleIterations);
        render();
    }
    m_dragPin.reset();
    m_dragMoved = false;
}

void PuppetTool::commit()
{
    if (!m_session)
        return;
    const std::unique_ptr<Session> s = std::move(m_session);
    m_dragPin.reset();
    m_selectedPin.reset();
    if (s->changed)
        m_document->endPaint(s->before);
    else
        m_document->cancelPaint();
    m_repaint();
}

void PuppetTool::cancel()
{
    if (!m_session)
        return;
    const std::unique_ptr<Session> s = std::move(m_session);
    m_dragPin.reset();
    m_selectedPin.reset();
    *s->image = s->before;
    m_document->notifyPainted(s->shown | boundsOf(s->before));
    m_document->cancelPaint();
    m_repaint();
}

bool PuppetTool::keyPress(int key)
{
    if (!m_session)
        return false;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter: commit(); return true;
    case Qt::Key_Escape: cancel(); return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_selectedPin && !m_dragPin) {
            m_session->rig->removePin(*m_selectedPin);
            m_selectedPin.reset();
            if (m_session->changed)
                render();
            m_repaint();
        }
        return true;
    default: return false;
    }
}

void PuppetTool::settingsChanged()
{
    m_repaint(); // the mesh may be shown or hidden
    if (!m_session || m_dragPin)
        return;
    m_session->rig->setSettings(rigSettings(m_toolState->puppetSettings()));
    m_selectedPin.reset();
    if (m_session->changed)
        render();
}

void PuppetTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    if (!m_session)
        return;
    const QTransform toView = view.documentToView();
    auto map = [&](puppet::Vec2 p) { return toView.map(QPointF(p.x, p.y)); };
    const QPalette &palette = QApplication::palette();
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_toolState->puppetSettings().showMesh) {
        QColor line = palette.color(QPalette::Highlight);
        line.setAlpha(150);
        painter.setPen(QPen(line, 1));
        painter.setBrush(Qt::NoBrush);
        for (const auto &mesh : m_session->rig->meshes())
            for (const auto &t : mesh.rest.triangles) {
                const QPointF a = map(mesh.deformed[std::size_t(t[0])]), b = map(mesh.deformed[std::size_t(t[1])]),
                              c = map(mesh.deformed[std::size_t(t[2])]);
                painter.drawLine(a, b);
                painter.drawLine(b, c);
                painter.drawLine(c, a);
            }
    }

    const auto &pins = m_session->rig->pins();
    for (std::size_t p = 0; p < pins.size(); ++p) {
        const bool selected = m_selectedPin && *m_selectedPin == int(p);
        painter.setPen(QPen(Qt::black, 1.5));
        painter.setBrush(selected ? palette.color(QPalette::Highlight) : QColor(Qt::white));
        painter.drawEllipse(map(pins[p].position), kPinRadius, kPinRadius);
    }
}

} // namespace mothion::canvas
