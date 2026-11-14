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

namespace {

// The folder name of each brush category, under brushes/.
QString folderName(ToolId category)
{
    switch (category) {
    case ToolId::Pencil: return QStringLiteral("pencil");
    case ToolId::Pen: return QStringLiteral("pen");
    case ToolId::Watercolor: return QStringLiteral("watercolor");
    case ToolId::Airbrush: return QStringLiteral("airbrush");
    case ToolId::Decoration: return QStringLiteral("decoration");
    default: return QStringLiteral("brush");
    }
}

} // namespace

QString BrushLibrary::categoryFolder(ToolId category) const
{
    return storageFolder() + QLatin1Char('/') + folderName(ToolState::isBrushTool(category) ? category : ToolId::Brush);
}

void BrushLibrary::loadStored()
{
    auto load = [this](const QString &folder, ToolId category) {
        const QDir dir(folder);
        for (const QString &file : dir.entryList({QStringLiteral("*.sut")}, QDir::Files, QDir::Name))
            if (auto imported = tools::importSut(dir.filePath(file))) {
                imported->preset.tool = category;
                imported->preset.storedFile = dir.filePath(file);
                imported->preset.id = QStringLiteral("imported/") + file;
                m_imported << imported->preset;
            }
    };
    load(storageFolder(), ToolId::Brush); // from before the categories
    for (const ToolId category : kBrushTools)
        load(categoryFolder(category), category);
}

QList<BrushPreset> BrushLibrary::presets(ToolId tool) const
{
    QList<BrushPreset> list = builtInPresets(tool);
    for (const BrushPreset &p : m_imported)
        if (p.tool == tool)
            list << p;
    return list;
}

bool BrushLibrary::importSut(const QString &path, ToolId category, QString *error, QStringList *warnings)
{
    if (!ToolState::isBrushTool(category))
        category = ToolId::Brush;
    auto imported = tools::importSut(path, error);
    if (!imported)
        return false;

    // Same content → same stored name, so importing twice does not duplicate.
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot read %1").arg(path);
        return false;
    }
    const QByteArray hash = QCryptographicHash::hash(in.readAll(), QCryptographicHash::Sha1).toHex().left(12);
    in.close();
    const QString name = QFileInfo(path).completeBaseName() + QLatin1Char('-') + QString::fromLatin1(hash)
                         + QStringLiteral(".sut");
    for (const BrushPreset &known : std::as_const(m_imported))
        if (QFileInfo(known.storedFile).fileName() == name) { // already here, in whatever category
            if (warnings)
                *warnings = imported->warnings;
            return true;
        }
    const QString folder = categoryFolder(category);
    QDir().mkpath(folder);
    const QString stored = folder + QLatin1Char('/') + name;
    if (!QFileInfo::exists(stored) && !QFile::copy(path, stored)) {
        if (error)
            *error = tr("Cannot copy the brush to %1").arg(folder);
        return false;
    }
    if (warnings)
        *warnings = imported->warnings;
    imported->preset.tool = category;
    imported->preset.storedFile = stored;
    imported->preset.id = QStringLiteral("imported/") + name;
    m_imported << imported->preset;
    emit presetsChanged();
    return true;
}

bool BrushLibrary::moveToCategory(const BrushPreset &preset, ToolId category, QString *error)
{
    if (!ToolState::isBrushTool(category) || preset.storedFile.isEmpty())
        return false;
    for (BrushPreset &held : m_imported) {
        if (held.storedFile != preset.storedFile)
            continue;
        if (held.tool == category)
            return true;
        const QString folder = categoryFolder(category);
        QDir().mkpath(folder);
        const QString moved = folder + QLatin1Char('/') + QFileInfo(held.storedFile).fileName();
        if (!QFile::rename(held.storedFile, moved)) {
            if (error)
                *error = tr("Cannot move the brush to %1").arg(folder);
            return false;
        }
        held.storedFile = moved;
        held.tool = category;
        emit presetsChanged();
        return true;
    }
    return false;
}

} // namespace mothion::tools
