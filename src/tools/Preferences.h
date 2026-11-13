#pragma once

#include <QColor>
#include <QObject>

namespace mothion::tools {

// The settings a person chooses in the Settings window and the app reads as it goes (the theme and the keys
// have their own homes: ui::Theme and ui::Shortcuts). Kept in QSettings ("preferences/*"); each set saves at
// once and says `changed()`, so what depends on them follows without a restart.
class Preferences : public QObject
{
    Q_OBJECT

public:
    static Preferences &instance();

    // Whether the Discord presence is on ("Mothion", the time and the frame).
    bool discordEnabled() const { return m_discord; }
    void setDiscordEnabled(bool on);

    // How many projects "Open recent" remembers.
    int recentLimit() const { return m_recentLimit; }
    void setRecentLimit(int limit);

    // One step of zoom in or out, in % of the size (25: a step is 1.25 times), and of rotation, in degrees.
    int zoomStepPercent() const { return m_zoomStep; }
    void setZoomStepPercent(int percent);
    double zoomFactor() const { return 1.0 + m_zoomStep / 100.0; }
    int rotateStep() const { return m_rotateStep; }
    void setRotateStep(int degrees);

    // A new project: the size (px), the frame rate, the length in frames and the color of the paper.
    int newWidth() const { return m_newWidth; }
    int newHeight() const { return m_newHeight; }
    void setNewSize(int width, int height);
    int newFrameRate() const { return m_newFps; }
    void setNewFrameRate(int fps);
    int newFrames() const { return m_newFrames; }
    void setNewFrames(int frames);
    QColor paperColor() const { return m_paper; }
    void setPaperColor(const QColor &color);

    // The undo history: the steps kept (it also stops at 1 GiB of undo data).
    int undoSteps() const { return m_undoSteps; }
    void setUndoSteps(int steps);

    // The pressure a mouse draws with, in % (a pen sends its own).
    int mousePressurePercent() const { return m_mousePressure; }
    void setMousePressurePercent(int percent);

    // The brush's outline, at its size, under the pen over the canvas; the system pointer is not shown
    // over it then.
    // How the Transform tool reads the pixels when it scales or turns them: a ResampleFilter's number (see
    // canvas/Resample.h; 2 is bicubic).
    int transformFilter() const { return m_transformFilter; }
    void setTransformFilter(int filter);
    bool brushOutline() const { return m_brushOutline; }
    void setBrushOutline(bool on);

    // The mode the vector eraser was last set to (a VectorEraserSettings::Mode's number: 0 is "like a brush"), so that
    // it is as it was at the next start. Not a choice of the Settings window: the tool's properties keep it.
    int vectorEraserMode() const { return m_vectorEraserMode; }
    void setVectorEraserMode(int mode);

    // Minutes between the autosaves of a project that has unsaved work (0: no autosave).
    int autosaveMinutes() const { return m_autosaveMinutes; }
    void setAutosaveMinutes(int minutes);

    // Everything back to the defaults.
    void resetAll();

    static constexpr int kDefaultRecentLimit = 10;
    static constexpr int kDefaultZoomStep = 25;
    static constexpr int kDefaultRotateStep = 15;
    static constexpr int kDefaultWidth = 1920;
    static constexpr int kDefaultHeight = 1080;
    static constexpr int kDefaultFrameRate = 24;
    static constexpr int kDefaultFrames = 120;
    static constexpr int kDefaultUndoSteps = 500;
    static constexpr int kDefaultAutosaveMinutes = 5;

signals:
    void changed();

private:
    Preferences();
    void save(const char *key, const QVariant &value);

    bool m_discord = true;
    int m_recentLimit = kDefaultRecentLimit;
    int m_zoomStep = kDefaultZoomStep;
    int m_rotateStep = kDefaultRotateStep;
    bool m_brushOutline = true;
    int m_transformFilter = 2;
    int m_newWidth = kDefaultWidth;
    int m_newHeight = kDefaultHeight;
    int m_newFps = kDefaultFrameRate;
    int m_newFrames = kDefaultFrames;
    QColor m_paper = Qt::white;
    int m_undoSteps = kDefaultUndoSteps;
    int m_mousePressure = 100;
    int m_autosaveMinutes = kDefaultAutosaveMinutes;
    int m_vectorEraserMode = 0;
};

} // namespace mothion::tools
