#include "tools/BrushLibrary.h"

#include "tools/SutImporter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace mothion::tools {

BrushLibrary::BrushLibrary(QObject *parent)
    : QObject(parent)
{
    loadStored();
}

QString BrushLibrary::storageFolder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/brushes");
}

void BrushLibrary::loadStored()
{
    const QDir dir(storageFolder());
    const QStringList files = dir.entryList({QStringLiteral("*.sut")}, QDir::Files, QDir::Name);
    for (const QString &file : files)
        if (auto imported = tools::importSut(dir.filePath(file)))
            m_imported << imported->preset;
}

QList<BrushPreset> BrushLibrary::presets(ToolId tool) const
{
    QList<BrushPreset> list = builtInPresets(tool);
    for (const BrushPreset &p : m_imported)
        if (p.tool == tool)
            list << p;
    return list;
}

bool BrushLibrary::importSut(const QString &path, QString *error, QStringList *warnings)
{
    auto imported = tools::importSut(path, error);
    if (!imported)
        return false;

    // Same content → same stored name, so importing twice does not duplicate.
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Impossibile leggere %1").arg(path);
        return false;
    }
    const QByteArray hash = QCryptographicHash::hash(in.readAll(), QCryptographicHash::Sha1).toHex().left(12);
    in.close();
    QDir().mkpath(storageFolder());
    const QString stored = storageFolder() + QLatin1Char('/') + QFileInfo(path).completeBaseName()
                           + QLatin1Char('-') + QString::fromLatin1(hash) + QStringLiteral(".sut");
    const bool known = QFileInfo::exists(stored);
    if (!known && !QFile::copy(path, stored)) {
        if (error)
            *error = tr("Impossibile copiare il pennello in %1").arg(storageFolder());
        return false;
    }
    if (warnings)
        *warnings = imported->warnings;
    if (!known) {
        m_imported << imported->preset;
        emit presetsChanged();
    }
    return true;
}

} // namespace mothion::tools
