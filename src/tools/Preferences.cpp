#include "tools/Preferences.h"

#include <QSettings>
#include <QVariant>

#include <algorithm>

namespace mothion::tools {

namespace {

constexpr auto kDiscord = "discord";
constexpr auto kRecentLimit = "recentLimit";
constexpr auto kZoomStep = "zoomStep";
constexpr auto kRotateStep = "rotateStep";
constexpr auto kBrushOutline = "brushOutline";
constexpr auto kTransformFilter = "transformFilter";
constexpr auto kNewWidth = "newWidth";
constexpr auto kNewHeight = "newHeight";
constexpr auto kNewFps = "newFrameRate";
constexpr auto kNewFrames = "newFrames";
constexpr auto kPaper = "paperColor";
constexpr auto kUndoSteps = "undoSteps";
constexpr auto kMousePressure = "mousePressure";
constexpr auto kAutosave = "autosaveMinutes";
constexpr auto kVectorEraserMode = "vectorEraserMode";

QString path(const char *key)
{
    return QStringLiteral("preferences/") + QLatin1String(key);
}

} // namespace

Preferences &Preferences::instance()
{
    static Preferences preferences;
    return preferences;
}

Preferences::Preferences()
{
    const QSettings settings;
    m_discord = settings.value(path(kDiscord), true).toBool();
    m_recentLimit = std::clamp(settings.value(path(kRecentLimit), kDefaultRecentLimit).toInt(), 1, 30);
    m_zoomStep = std::clamp(settings.value(path(kZoomStep), kDefaultZoomStep).toInt(), 5, 100);
    m_rotateStep = std::clamp(settings.value(path(kRotateStep), kDefaultRotateStep).toInt(), 1, 90);
    m_brushOutline = settings.value(path(kBrushOutline), true).toBool();
    m_transformFilter = std::clamp(settings.value(path(kTransformFilter), 2).toInt(), 0, 3);
    m_newWidth = std::clamp(settings.value(path(kNewWidth), kDefaultWidth).toInt(), 16, 16000);
    m_newHeight = std::clamp(settings.value(path(kNewHeight), kDefaultHeight).toInt(), 16, 16000);
    m_newFps = std::clamp(settings.value(path(kNewFps), kDefaultFrameRate).toInt(), 1, 120);
    m_newFrames = std::clamp(settings.value(path(kNewFrames), kDefaultFrames).toInt(), 1, 5000);
    const QColor paper(settings.value(path(kPaper), QStringLiteral("#ffffff")).toString());
    m_paper = paper.isValid() ? paper : QColor(Qt::white);
    m_undoSteps = std::clamp(settings.value(path(kUndoSteps), kDefaultUndoSteps).toInt(), 10, 5000);
    m_mousePressure = std::clamp(settings.value(path(kMousePressure), 100).toInt(), 10, 100);
    m_autosaveMinutes = std::clamp(settings.value(path(kAutosave), kDefaultAutosaveMinutes).toInt(), 0, 120);
    m_vectorEraserMode = std::clamp(settings.value(path(kVectorEraserMode), 0).toInt(), 0, 3);
}

void Preferences::save(const char *key, const QVariant &value)
{
    QSettings().setValue(path(key), value);
    emit changed();
}

void Preferences::setDiscordEnabled(bool on)
{
    if (on == m_discord)
        return;
    m_discord = on;
    save(kDiscord, on);
}

void Preferences::setRecentLimit(int limit)
{
    limit = std::clamp(limit, 1, 30);
    if (limit == m_recentLimit)
        return;
    m_recentLimit = limit;
    save(kRecentLimit, limit);
}

void Preferences::setZoomStepPercent(int percent)
{
    percent = std::clamp(percent, 5, 100);
    if (percent == m_zoomStep)
        return;
    m_zoomStep = percent;
    save(kZoomStep, percent);
}

void Preferences::setRotateStep(int degrees)
{
    degrees = std::clamp(degrees, 1, 90);
    if (degrees == m_rotateStep)
        return;
    m_rotateStep = degrees;
    save(kRotateStep, degrees);
}

void Preferences::setTransformFilter(int filter)
{
    filter = std::clamp(filter, 0, 3);
    if (filter == m_transformFilter)
        return;
    m_transformFilter = filter;
    save(kTransformFilter, filter);
}

void Preferences::setVectorEraserMode(int mode)
{
    mode = std::clamp(mode, 0, 3);
    if (mode == m_vectorEraserMode)
        return;
    m_vectorEraserMode = mode;
    save(kVectorEraserMode, mode);
}

void Preferences::setBrushOutline(bool on)
{
    if (on == m_brushOutline)
        return;
    m_brushOutline = on;
    save(kBrushOutline, on);
}

void Preferences::setNewSize(int width, int height)
{
    width = std::clamp(width, 16, 16000);
    height = std::clamp(height, 16, 16000);
    if (width == m_newWidth && height == m_newHeight)
        return;
    m_newWidth = width;
    m_newHeight = height;
    QSettings settings;
    settings.setValue(path(kNewWidth), width);
    settings.setValue(path(kNewHeight), height);
    emit changed();
}

void Preferences::setNewFrameRate(int fps)
{
    fps = std::clamp(fps, 1, 120);
    if (fps == m_newFps)
        return;
    m_newFps = fps;
    save(kNewFps, fps);
}

void Preferences::setNewFrames(int frames)
{
    frames = std::clamp(frames, 1, 5000);
    if (frames == m_newFrames)
        return;
    m_newFrames = frames;
    save(kNewFrames, frames);
}

void Preferences::setPaperColor(const QColor &color)
{
    if (!color.isValid() || color == m_paper)
        return;
    m_paper = color;
    save(kPaper, color.name());
}

void Preferences::setUndoSteps(int steps)
{
    steps = std::clamp(steps, 10, 5000);
    if (steps == m_undoSteps)
        return;
    m_undoSteps = steps;
    save(kUndoSteps, steps);
}

void Preferences::setAutosaveMinutes(int minutes)
{
    minutes = std::clamp(minutes, 0, 120);
    if (minutes == m_autosaveMinutes)
        return;
    m_autosaveMinutes = minutes;
    save(kAutosave, minutes);
}

void Preferences::setMousePressurePercent(int percent)
{
    percent = std::clamp(percent, 10, 100);
    if (percent == m_mousePressure)
        return;
    m_mousePressure = percent;
    save(kMousePressure, percent);
}

void Preferences::resetAll()
{
    setNewSize(kDefaultWidth, kDefaultHeight);
    setNewFrameRate(kDefaultFrameRate);
    setNewFrames(kDefaultFrames);
    setPaperColor(Qt::white);
    setUndoSteps(kDefaultUndoSteps);
    setMousePressurePercent(100);
    setDiscordEnabled(true);
    setRecentLimit(kDefaultRecentLimit);
    setZoomStepPercent(kDefaultZoomStep);
    setRotateStep(kDefaultRotateStep);
    setBrushOutline(true);
    setTransformFilter(2);
    setAutosaveMinutes(kDefaultAutosaveMinutes);
}

} // namespace mothion::tools
