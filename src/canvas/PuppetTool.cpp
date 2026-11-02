#include "canvas/PuppetTool.h"

#include "canvas/SelectionMask.h"
#include "canvas/TileQt.h"
#include "canvas/ViewTransform.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"

#include <QPainter>
#include <QPalette>
#include <QApplication>

#include <algorithm>
#include <cmath>

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
    using document::DocumentController;
    auto retry = [this] { m_previewFailed = false; };
    m_documentConnections = {
        QObject::connect(m_document, &DocumentController::pixelsChanged, m_document, retry),
        QObject::connect(m_document, &DocumentController::structureChanged, m_document, retry),
        QObject::connect(m_document, &DocumentController::currentCelChanged, m_document, retry),
        QObject::connect(m_document, &DocumentController::currentFrameChanged, m_document, retry),
        QObject::connect(m_document, &DocumentController::selectionChanged, m_document, retry),
    };
}

PuppetTool::~PuppetTool()
{
    QObject::disconnect(m_settingsConnection);
    for (const QMetaObject::Connection &c : m_documentConnections)
        QObject::disconnect(c);
}

bool PuppetTool::start(bool quiet)
{
    QString reason;
    const auto target = m_document->beginPaint(false, &reason);
    if (!target) {
        if (!quiet)
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
        if (!quiet)
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
        if (!quiet)
            m_status(QObject::tr("Nothing to warp here"));
        return false;
    }
    s->shown = area;
    m_session = std::move(s);
    clearSelection();
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

void PuppetTool::beginStep()
{
    m_session->stepStart = m_session->rig->state();
    m_session->stepChanged = false;
}

void PuppetTool::endStep()
{
    if (m_session && m_session->stepStart && m_session->stepChanged)
        pushStep(std::move(*m_session->stepStart));
    if (m_session)
        m_session->stepStart.reset();
}

void PuppetTool::pushStep(puppet::PuppetRig::State before)
{
    m_session->undo.push_back(std::move(before));
    m_session->redo.clear();
}

void PuppetTool::show()
{
    Session &s = *m_session;
    if (s.rig->isDeformed()) {
        render();
        return;
    }
    // Undeformed (back at the start, or pins only): the drawing as it was,
    // exactly, not warped again.
    *s.image = s.before;
    m_document->notifyPainted(s.shown | boundsOf(s.before));
    s.changed = false;
}

bool PuppetTool::undoStep()
{
    if (!canUndoStep())
        return false;
    Session &s = *m_session;
    s.redo.push_back(s.rig->state());
    s.rig->restore(s.undo.back());
    s.undo.pop_back();
    clearSelection();
    show();
    m_repaint();
    return true;
}

bool PuppetTool::redoStep()
{
    if (!canRedoStep())
        return false;
    Session &s = *m_session;
    s.undo.push_back(s.rig->state());
    s.rig->restore(s.redo.back());
    s.redo.pop_back();
    clearSelection();
    show();
    m_repaint();
    return true;
}

void PuppetTool::clearSelection()
{
    m_selectedPins.clear();
    m_localPoints.clear();
}

std::optional<std::size_t> PuppetTool::pickedPointAt(puppet::Vec2 at) const
{
    for (std::size_t i = 0; i < m_localPoints.size(); ++i)
        if (puppet::length(m_session->rig->position(m_localPoints[i]) - at) <= kGrabRadius / m_zoom)
            return i;
    return std::nullopt;
}

void PuppetTool::startPinDrag(puppet::Vec2 at)
{
    m_dragPins = m_selectedPins;
    m_dragStarts.clear();
    for (int pin : m_dragPins)
        m_dragStarts.push_back(m_session->rig->pins()[std::size_t(pin)].position);
    m_pressAt = at;
    m_draggingPins = true;
}

void PuppetTool::press(const QPointF &docPos, Qt::KeyboardModifiers modifiers)
{
    m_pointer = docPos;
    m_pointerKnown = true;
    m_previewFailed = false;
    if (!m_session && !start())
        return;
    beginStep();
    puppet::PuppetRig &rig = *m_session->rig;
    const puppet::Vec2 at{docPos.x(), docPos.y()};
    const tools::PuppetSettings settings = m_toolState->puppetSettings();
    const bool extend = modifiers & (Qt::ShiftModifier | Qt::ControlModifier);

    if (settings.local) {
        m_selectedPins.clear();
        if (extend) { // pick a point, or drop a picked one
            m_session->stepStart.reset();
            if (const auto picked = pickedPointAt(at))
                m_localPoints.erase(m_localPoints.begin() + std::ptrdiff_t(*picked));
            else if (const auto v = rig.vertexAt(at))
                m_localPoints.push_back(*v);
            else
                m_status(QObject::tr("Shift+click on the drawing to pick a point"));
            m_repaint();
            return;
        }
        if (pickedPointAt(at)) {
            m_localDrag = rig.beginLocalDrag(m_localPoints, at, settings.radius);
        } else {
            m_localPoints.clear();
            m_localDrag = rig.beginLocalDrag(at, settings.radius);
        }
        if (!m_localDrag) {
            m_session->stepStart.reset();
            m_status(QObject::tr("Drag on the drawing to bend it"));
        }
        m_repaint();
        return;
    }

    m_localPoints.clear();
    if (const auto pin = rig.pinAt(at, kGrabRadius / m_zoom)) {
        if (modifiers & Qt::AltModifier) {
            m_session->stepChanged = true;
            endStep();
            rig.removePin(*pin);
            m_selectedPins.clear(); // the indices after it moved down
            if (m_session->changed)
                render();
            m_repaint();
            return;
        }
        const auto selected = std::find(m_selectedPins.begin(), m_selectedPins.end(), *pin);
        if (extend && selected != m_selectedPins.end()) { // taken out: nothing to drag
            m_selectedPins.erase(selected);
            m_session->stepStart.reset();
            m_repaint();
            return;
        }
        if (extend)
            m_selectedPins.push_back(*pin);
        else if (selected == m_selectedPins.end())
            m_selectedPins = {*pin};
        startPinDrag(at);
    } else if (const auto added = rig.addPin(at)) {
        m_session->stepChanged = true; // a new pin is a step, even unmoved
        if (extend) {
            m_selectedPins.push_back(*added);
        } else {
            m_selectedPins = {*added};
            startPinDrag(at);
        }
    } else {
        m_session->stepStart.reset();
        m_status(QObject::tr("Click on the drawing to place a pin"));
    }
    m_repaint();
}

void PuppetTool::move(const QPointF &docPos, Qt::KeyboardModifiers)
{
    m_pointer = docPos;
    if (m_session && m_localDrag) {
        m_session->rig->localDragTo({docPos.x(), docPos.y()}, kDragIterations);
        m_session->stepChanged = true;
        render();
        return;
    }
    if (!m_session || !m_draggingPins)
        return;
    moveDraggedPins(docPos, kDragIterations);
    m_dragMoved = true;
    m_session->stepChanged = true;
    render();
}

void PuppetTool::moveDraggedPins(const QPointF &pen, int iterations)
{
    const puppet::Vec2 travel = puppet::Vec2{pen.x(), pen.y()} - m_pressAt;
    std::vector<puppet::Vec2> to;
    for (const puppet::Vec2 &start : m_dragStarts)
        to.push_back(start + travel);
    m_session->rig->movePins(m_dragPins, to, iterations);
}

void PuppetTool::release(const QPointF &docPos, Qt::KeyboardModifiers)
{
    m_pointer = docPos;
    if (m_session && m_localDrag) {
        m_session->rig->localDragTo({docPos.x(), docPos.y()}, kSettleIterations);
        m_session->rig->endLocalDrag();
        m_localDrag = false;
        render();
        endStep();
        return;
    }
    if (!m_session || !m_draggingPins)
        return;
    if (m_dragMoved) { // settle the shape where the pen left it
        moveDraggedPins(docPos, kSettleIterations);
        render();
    }
    m_draggingPins = false;
    m_dragMoved = false;
    endStep();
}

void PuppetTool::commit()
{
    m_localDrag = false;
    if (!m_session)
        return;
    const std::unique_ptr<Session> s = std::move(m_session);
    m_draggingPins = false;
    clearSelection();
    if (s->changed)
        m_document->endPaint(s->before);
    else
        m_document->cancelPaint();
    m_repaint();
}

void PuppetTool::cancel()
{
    m_localDrag = false;
    if (!m_session)
        return;
    const std::unique_ptr<Session> s = std::move(m_session);
    m_draggingPins = false;
    clearSelection();
    *s->image = s->before;
    m_document->notifyPainted(s->shown | boundsOf(s->before));
    m_document->cancelPaint();
    m_repaint();
}

bool PuppetTool::wantsKey(int key) const
{
    // Radius mode: + and − size the radius, before - goes to the next frame.
    return m_toolState->puppetSettings().local
           && (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus);
}

bool PuppetTool::keyPress(int key)
{
    if (m_toolState->puppetSettings().local) {
        switch (key) {
        case Qt::Key_Plus:
        case Qt::Key_Equal: stepRadius(+1); return true; // + shares a key with = on some layouts
        case Qt::Key_Minus: stepRadius(-1); return true;
        default: break;
        }
    }
    if (!m_session)
        return false;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter: commit(); return true;
    case Qt::Key_Escape: cancel(); return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (!m_selectedPins.empty() && !m_draggingPins) {
            pushStep(m_session->rig->state());
            std::vector<int> pins = m_selectedPins; // from the last, so the others keep their index
            std::sort(pins.rbegin(), pins.rend());
            for (int pin : pins)
                m_session->rig->removePin(pin);
            m_selectedPins.clear();
            if (m_session->changed)
                render();
            m_repaint();
        }
        return true;
    default: return false;
    }
}

void PuppetTool::activated()
{
    m_previewFailed = false;
    startForPreview();
    m_repaint();
}

void PuppetTool::startForPreview()
{
    if (m_session || m_previewFailed || !m_toolState->puppetSettings().local)
        return;
    m_previewFailed = !start(true);
}

bool PuppetTool::hover(const QPointF &docPos)
{
    m_pointer = docPos;
    m_pointerKnown = true;
    if (!m_toolState->puppetSettings().local)
        return false;
    startForPreview();
    return true; // the circle and the point follow
}

std::optional<QPointF> PuppetTool::grabPoint() const
{
    if (!m_session || !m_pointerKnown)
        return std::nullopt;
    const auto at = m_session->rig->localGrabPoint({m_pointer.x(), m_pointer.y()});
    if (!at)
        return std::nullopt;
    return QPointF(at->x, at->y);
}

bool PuppetTool::wheel(double steps, Qt::KeyboardModifiers modifiers)
{
    tools::PuppetSettings s = m_toolState->puppetSettings();
    if (!s.local || modifiers != Qt::NoModifier)
        return false; // Ctrl/Shift+wheel still zoom and rotate the view
    stepRadius(steps);
    return true;
}

void PuppetTool::stepRadius(double steps)
{
    tools::PuppetSettings s = m_toolState->puppetSettings();
    s.radius *= std::pow(1.15, steps);
    m_toolState->setPuppetSettings(s);
    m_repaint();
}

void PuppetTool::settingsChanged()
{
    m_repaint(); // the mesh may be shown or hidden
    if (m_session && m_localDrag) { // a radius changed mid-drag acts at once
        m_session->rig->setLocalRadius(m_toolState->puppetSettings().radius, kSettleIterations);
        m_session->stepChanged = true;
        render();
        return;
    }
    if (!m_session || isActive())
        return;
    const puppet::RigSettings now = rigSettings(m_toolState->puppetSettings());
    const puppet::RigSettings was = m_session->rig->settings();
    if (now.density == was.density && now.expand == was.expand)
        return; // the radius, the mode, the mesh shown: nothing to remesh
    m_session->rig->setSettings(now);
    m_localPoints.clear(); // their vertices are gone
    if (m_session->changed)
        render();
}

void PuppetTool::paintOverlay(QPainter &painter, const ViewTransform &view) const
{
    const QTransform toView = view.documentToView();
    painter.setRenderHint(QPainter::Antialiasing);
    // Radius mode: the points a drag takes (or has taken), each with the disc
    // that bends with it; off the drawing, just the radius around the pen.
    if (m_toolState->puppetSettings().local && m_pointerKnown) {
        const double r = m_toolState->puppetSettings().radius * view.zoom();
        struct Mark
        {
            QPointF at;
            bool taken; // picked or being dragged, not just under the pen
        };
        std::vector<Mark> marks;
        if (m_session && m_session->rig->isLocalDragging()) {
            for (const puppet::Vec2 &p : m_session->rig->localDragPoints())
                marks.push_back({QPointF(p.x, p.y), true});
        } else if (m_session) {
            for (const auto &v : m_localPoints) {
                const puppet::Vec2 p = m_session->rig->position(v);
                marks.push_back({QPointF(p.x, p.y), true});
            }
            const std::optional<QPointF> hovered = grabPoint();
            if (hovered && !pickedPointAt({m_pointer.x(), m_pointer.y()}))
                marks.push_back({*hovered, false});
        }
        auto circle = [&](QPointF c) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
            painter.drawEllipse(c, r, r);
            painter.setPen(QPen(QColor(255, 255, 255, 160), 1.0));
            painter.drawEllipse(c, r - 1, r - 1);
        };
        if (marks.empty())
            circle(toView.map(m_pointer));
        for (const Mark &m : marks)
            circle(toView.map(m.at));
        const QPalette &palette = QApplication::palette();
        for (const Mark &m : marks) {
            painter.setPen(QPen(Qt::black, 1.5));
            painter.setBrush(m.taken ? palette.color(QPalette::Highlight) : QColor(Qt::white));
            painter.drawEllipse(toView.map(m.at), kPinRadius, kPinRadius);
        }
    }
    if (!m_session)
        return;
    auto map = [&](puppet::Vec2 p) { return toView.map(QPointF(p.x, p.y)); };
    const QPalette &palette = QApplication::palette();

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
        const bool selected = std::find(m_selectedPins.begin(), m_selectedPins.end(), int(p)) != m_selectedPins.end();
        painter.setPen(QPen(Qt::black, 1.5));
        painter.setBrush(selected ? palette.color(QPalette::Highlight) : QColor(Qt::white));
        painter.drawEllipse(map(pins[p].position), kPinRadius, kPinRadius);
    }
}

} // namespace mothion::canvas
