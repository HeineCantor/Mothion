#pragma once

#include "tools/BrushPreset.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace mothion::tools {

// Brushes the user added, on top of the built-in presets.
//
// Importing a .sut copies the file into the application data folder, in the
// folder of its brush category (brushes/pencil/, brushes/pen/...), and reads
// it from there at every start, so the original can be moved or deleted. The
// copy is the storage until Mothion has a brush format of its own. Files
// right in brushes/ (imported before there were categories) are Brush ones.
class BrushLibrary : public QObject
{
    Q_OBJECT

public:
    explicit BrushLibrary(QObject *parent = nullptr);

    // Built-in presets of `tool` followed by the imported ones.
    QList<BrushPreset> presets(ToolId tool) const;

    // Imports a .sut into the brush category `category` (ToolState::isBrushTool) and keeps a copy. On success
    // `warnings` lists what the engine does not reproduce; on failure `error` says why. A brush imported again (the
    // same file) stays where it is.
    bool importSut(const QString &path, ToolId category, QString *error = nullptr, QStringList *warnings = nullptr);
    // Moves an imported sub tool (one with a storedFile) to another brush category. False, with `error`, if it
    // cannot be moved.
    bool moveToCategory(const BrushPreset &preset, ToolId category, QString *error = nullptr);

    QString storageFolder() const;
    // The folder of a brush category's imported files.
    QString categoryFolder(ToolId category) const;

signals:
    void presetsChanged();

private:
    void loadStored();

    QList<BrushPreset> m_imported;
};

} // namespace mothion::tools
