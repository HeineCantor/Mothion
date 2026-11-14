#include "tools/SubToolMemory.h"

#include "tools/BrushLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace mothion::tools {

namespace {

// The paint tools, which have sub tools that remember their options.
bool remembers(ToolId tool)
{
    return ToolState::isPaintTool(tool);
}

QString toolKey(ToolId tool)
{
    return QString::number(int(tool));
}

QJsonObject toJson(const SubToolValues &v)
{
    return {
        {QStringLiteral("size"), v.size},
        {QStringLiteral("opacity"), v.opacity},
        {QStringLiteral("curve"), QJsonArray{v.curve.control1().x, v.curve.control1().y, v.curve.control2().x,
                                             v.curve.control2().y}},
        {QStringLiteral("smoothing"), v.smoothing},
        {QStringLiteral("sharpCorners"), v.sharpCorners},
        {QStringLiteral("revision"), v.revision},
        {QStringLiteral("revisionBySpeed"), v.revisionBySpeed},
        {QStringLiteral("revisionCurves"), v.revisionCurves},
    };
}

// Missing entries keep the defaults' values.
SubToolValues fromJson(const QJsonObject &o, SubToolValues v)
{
    v.size = o.value(QStringLiteral("size")).toDouble(v.size);
    v.opacity = o.value(QStringLiteral("opacity")).toDouble(v.opacity);
    const QJsonArray c = o.value(QStringLiteral("curve")).toArray();
    if (c.size() == 4)
        v.curve = PressureCurve({float(c[0].toDouble()), float(c[1].toDouble())},
                                {float(c[2].toDouble()), float(c[3].toDouble())});
    v.smoothing = o.value(QStringLiteral("smoothing")).toDouble(v.smoothing);
    v.sharpCorners = o.value(QStringLiteral("sharpCorners")).toBool(v.sharpCorners);
    v.revision = o.value(QStringLiteral("revision")).toInt(v.revision);
    v.revisionBySpeed = o.value(QStringLiteral("revisionBySpeed")).toBool(v.revisionBySpeed);
    v.revisionCurves = o.value(QStringLiteral("revisionCurves")).toBool(v.revisionCurves);
    return v;
}

} // namespace

SubToolValues SubToolValues::defaultsOf(const BrushPreset &preset)
{
    const BrushDefinitionPtr brush = preset.brush ? preset.brush : roundBrush();
    SubToolValues v;
    v.size = preset.size;
    v.opacity = preset.opacity;
    v.curve = preset.curve;
    v.smoothing = preset.smoothing;
    v.sharpCorners = preset.sharpCorners;
    v.revision = brush->style.revision;
    v.revisionBySpeed = brush->style.revisionBySpeed;
    v.revisionCurves = brush->style.revisionCurves;
    return v;
}

SubToolMemory::SubToolMemory(ToolState *state, BrushLibrary *library, QString file, QObject *parent)
    : QObject(parent)
    , m_state(state)
    , m_library(library)
    , m_file(std::move(file))
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &SubToolMemory::save);
    load();

    // What the person changes goes to the sub tool in use; a paint tool picked gives back its sub tool as it was.
    auto changed = [this] {
        if (!m_applying && remembers(m_state->activeTool()))
            capture(m_state->activeTool());
    };
    connect(m_state, &ToolState::toolSettingsChanged, this, [this, changed](ToolId tool) {
        if (tool == m_state->activeTool())
            changed();
    });
    connect(m_state, &ToolState::pressureCurveChanged, this, changed);
    connect(m_state, &ToolState::smoothingChanged, this, changed);
    connect(m_state, &ToolState::sharpCornersChanged, this, changed);
    connect(m_state, &ToolState::activeToolChanged, this, [this](ToolId tool) {
        if (remembers(tool))
            restore(tool);
    });
    connect(m_library, &BrushLibrary::presetsChanged, this, [this] { m_lookupTool = ToolId::Hand; });

    // Every paint tool starts with its sub tool in use: the one selected last time, or its first; the active tool
    // last, so the shared options (curve, stabilization) are its own.
    auto start = [this](ToolId tool) {
        if (!find(tool, current(tool))) {
            const QList<BrushPreset> presets = m_library->presets(tool);
            if (presets.isEmpty())
                return;
            m_current[tool] = presets.first().id;
        }
        restore(tool);
    };
    for (const ToolId tool : kBrushTools)
        if (tool != m_state->activeTool())
            start(tool);
    if (m_state->activeTool() != ToolId::Eraser)
        start(ToolId::Eraser);
    if (remembers(m_state->activeTool()))
        start(m_state->activeTool());
}

QString SubToolMemory::defaultFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/subtools.json");
}

SubToolMemory::~SubToolMemory()
{
    if (m_saveTimer.isActive())
        save();
}

QString SubToolMemory::current(ToolId tool) const
{
    const auto it = m_current.find(tool);
    return it == m_current.end() ? QString() : it->second;
}

const BrushPreset *SubToolMemory::find(ToolId tool, const QString &id) const
{
    if (id.isEmpty())
        return nullptr;
    if (m_lookupTool != tool) {
        m_lookup = m_library->presets(tool);
        m_lookupTool = tool;
    }
    for (const BrushPreset &preset : std::as_const(m_lookup))
        if (preset.id == id)
            return &preset;
    return nullptr;
}

SubToolValues SubToolMemory::valuesOf(const BrushPreset &preset) const
{
    const auto it = m_values.constFind(preset.id);
    return it != m_values.constEnd() ? *it : SubToolValues::defaultsOf(preset);
}

bool SubToolMemory::isModified(const BrushPreset &preset) const
{
    return m_values.contains(preset.id);
}

void SubToolMemory::select(const BrushPreset &preset)
{
    if (!remembers(preset.tool) || preset.id.isEmpty()) {
        applyPreset(*m_state, preset); // a sub tool that only sets a mode or picks a tool
        return;
    }
    const bool changed = current(preset.tool) != preset.id;
    m_current[preset.tool] = preset.id;
    apply(preset, valuesOf(preset));
    if (changed) {
        scheduleSave();
        emit currentChanged(preset.tool);
    }
}

void SubToolMemory::reset(const BrushPreset &preset)
{
    const bool was = m_values.remove(preset.id) > 0;
    if (current(preset.tool) == preset.id)
        apply(preset, SubToolValues::defaultsOf(preset));
    if (was) {
        scheduleSave();
        emit modifiedChanged();
    }
}

void SubToolMemory::apply(const BrushPreset &preset, const SubToolValues &values)
{
    m_applying = true;
    const ToolId tool = preset.tool;
    m_state->setSize(tool, values.size);
    m_state->setOpacity(tool, values.opacity);
    // The shared options are the active tool's: another tool's go in when it is picked (restore()).
    if (tool == m_state->activeTool() || !remembers(m_state->activeTool())) {
        m_state->setPressureCurve(values.curve);
        m_state->setSmoothing(values.smoothing);
        m_state->setSharpCorners(values.sharpCorners);
    }
    BrushDefinitionPtr brush = preset.brush ? preset.brush : roundBrush();
    if (brush->style.revision != values.revision || brush->style.revisionBySpeed != values.revisionBySpeed
        || brush->style.revisionCurves != values.revisionCurves) {
        auto edited = std::make_shared<BrushDefinition>(*brush);
        edited->style.revision = values.revision;
        edited->style.revisionBySpeed = values.revisionBySpeed;
        edited->style.revisionCurves = values.revisionCurves;
        brush = std::move(edited);
    }
    m_state->setBrush(tool, brush);
    m_applying = false;
}

void SubToolMemory::capture(ToolId tool)
{
    const BrushPreset *preset = find(tool, current(tool));
    if (!preset)
        return;
    SubToolValues now;
    const BrushSettings settings = m_state->settings(tool);
    now.size = settings.size;
    now.opacity = settings.opacity;
    now.curve = m_state->pressureCurve();
    now.smoothing = m_state->smoothing();
    now.sharpCorners = m_state->sharpCorners();
    const brush::BrushStyle &style = m_state->brush(tool)->style;
    now.revision = style.revision;
    now.revisionBySpeed = style.revisionBySpeed;
    now.revisionCurves = style.revisionCurves;
    const bool wasModified = m_values.contains(preset->id);
    if (now == SubToolValues::defaultsOf(*preset)) {
        if (!wasModified)
            return;
        m_values.remove(preset->id);
    } else {
        const auto it = m_values.constFind(preset->id);
        if (it != m_values.constEnd() && *it == now)
            return;
        m_values.insert(preset->id, now);
    }
    scheduleSave();
    if (wasModified != m_values.contains(preset->id))
        emit modifiedChanged();
}

void SubToolMemory::restore(ToolId tool)
{
    if (const BrushPreset *preset = find(tool, current(tool)))
        apply(*preset, valuesOf(*preset));
}

void SubToolMemory::scheduleSave()
{
    if (!m_file.isEmpty())
        m_saveTimer.start();
}

void SubToolMemory::load()
{
    if (m_file.isEmpty())
        return;
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject current = root.value(QStringLiteral("current")).toObject();
    for (auto it = current.begin(); it != current.end(); ++it) {
        bool ok = false;
        const int tool = it.key().toInt(&ok);
        if (ok && tool >= 0 && tool < kToolCount && remembers(ToolId(tool)))
            m_current[ToolId(tool)] = it.value().toString();
    }
    // The values are read against the defaults of the sub tools there are now (one gone is dropped).
    const QJsonObject values = root.value(QStringLiteral("values")).toObject();
    for (const ToolId tool : kBrushTools)
        for (const BrushPreset &preset : m_library->presets(tool))
            if (values.contains(preset.id))
                m_values.insert(preset.id, fromJson(values.value(preset.id).toObject(), SubToolValues::defaultsOf(preset)));
    for (const BrushPreset &preset : m_library->presets(ToolId::Eraser))
        if (values.contains(preset.id))
            m_values.insert(preset.id, fromJson(values.value(preset.id).toObject(), SubToolValues::defaultsOf(preset)));
}

void SubToolMemory::save()
{
    m_saveTimer.stop();
    if (m_file.isEmpty())
        return;
    QJsonObject current;
    for (const auto &[tool, id] : m_current)
        current.insert(toolKey(tool), id);
    QJsonObject values;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it)
        values.insert(it.key(), toJson(*it));
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile out(m_file);
    if (!out.open(QIODevice::WriteOnly))
        return;
    out.write(QJsonDocument(QJsonObject{{QStringLiteral("current"), current}, {QStringLiteral("values"), values}})
                  .toJson(QJsonDocument::Indented));
    out.commit();
}

} // namespace mothion::tools
