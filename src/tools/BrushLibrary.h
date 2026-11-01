#pragma once

#include "tools/BrushPreset.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace mothion::tools {

// Brushes the user added, on top of the built-in presets.
//
// Importing a .sut copies the file into the application data folder
// (brushes/) and reads it from there at every start, so the original can
// be moved or deleted. The copy is the storage until Mothion has a brush
// format of its own.
class BrushLibrary : public QObject
{
    Q_OBJECT

public:
    explicit BrushLibrary(QObject *parent = nullptr);

    // Built-in presets of `tool` followed by the imported ones.
    QList<BrushPreset> presets(ToolId tool) const;

    // Imports a .sut and keeps a copy. On success `warnings` lists what the
    // engine does not reproduce; on failure `error` says why.
    bool importSut(const QString &path, QString *error = nullptr, QStringList *warnings = nullptr);

    QString storageFolder() const;

signals:
    void presetsChanged();

private:
    void loadStored();

    QList<BrushPreset> m_imported;
};

} // namespace mothion::tools
