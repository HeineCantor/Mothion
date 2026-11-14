#pragma once

#include "tools/BrushPreset.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <map>

namespace mothion::tools {

class BrushLibrary;

// The options of a paint sub tool that it remembers as its own (as in Clip Studio): its size and opacity, its pressure
// curve, its stabilization, its sharp corners and its correction after the stroke. The other options (tilt, start
// correction after the stroke) are the same for every brush.
struct SubToolValues
{
    double size = 10.0;
    double opacity = 1.0;
    PressureCurve curve;
    double smoothing = 0.3;
    bool sharpCorners = false;
    int revision = 0;
    bool revisionBySpeed = false;
    bool revisionCurves = false;

    // What the sub tool starts with.
    static SubToolValues defaultsOf(const BrushPreset &preset);
    bool operator==(const SubToolValues &) const = default;
};

// Which sub tool each paint tool (the brush categories and the eraser) has selected, and what each sub tool has been
// set to since: picking a sub tool, or coming back to its tool, gives it back as it was left. A sub tool that was
// never changed has its defaults. Both are kept in a file of the application data (subtools.json) and read at start.
class SubToolMemory : public QObject
{
    Q_OBJECT

public:
    // Kept in `file` (defaultFile() for the application's); empty: kept for the session only.
    SubToolMemory(ToolState *state, BrushLibrary *library, QString file = {}, QObject *parent = nullptr);
    // subtools.json in the application data folder.
    static QString defaultFile();
    ~SubToolMemory() override;

    // The selected sub tool of a paint tool (its id); empty if none was ever picked there.
    QString current(ToolId tool) const;
    // Makes `preset` the selected sub tool of its tool and puts its remembered values (or its defaults) in use.
    void select(const BrushPreset &preset);
    // Forgets what `preset` was set to; if it is in use, its defaults are put in use.
    void reset(const BrushPreset &preset);
    // Whether `preset` is set otherwise than its defaults.
    bool isModified(const BrushPreset &preset) const;
    // What `preset` is set to (its defaults if never changed).
    SubToolValues valuesOf(const BrushPreset &preset) const;

    // Writes the file now (it is otherwise written a moment after a change, and on destruction).
    void save();

signals:
    void currentChanged(mothion::tools::ToolId tool);
    void modifiedChanged();

private:
    void load();
    // The values in use now for the paint tool `tool`, into its selected sub tool.
    void capture(ToolId tool);
    // The selected sub tool of `tool` (its remembered values) put in use.
    void restore(ToolId tool);
    void apply(const BrushPreset &preset, const SubToolValues &values);
    const BrushPreset *find(ToolId tool, const QString &id) const;
    void scheduleSave();

    ToolState *m_state;
    BrushLibrary *m_library;
    QString m_file;
    std::map<ToolId, QString> m_current;
    QHash<QString, SubToolValues> m_values; // by sub tool id, only those changed from their defaults
    mutable QList<BrushPreset> m_lookup;    // the presets of the tool last looked up
    mutable ToolId m_lookupTool = ToolId::Hand;
    bool m_applying = false;
    QTimer m_saveTimer;
};

} // namespace mothion::tools
