#include "io/Autosave.h"

#include "core/document/Document.h"
#include "io/ProjectFile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace mothion::io::autosave {

namespace {

QString g_folder;

QString keyOf(const QString &projectPath)
{
    const QString normal = projectPath.isEmpty() ? QStringLiteral("untitled") : QFileInfo(projectPath).absoluteFilePath();
    return QString::fromLatin1(QCryptographicHash::hash(normal.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

QString nameOf(const QString &projectPath)
{
    return projectPath.isEmpty() ? QObject::tr("Untitled") : QFileInfo(projectPath).completeBaseName();
}

} // namespace

void setFolder(const QString &folder)
{
    g_folder = folder;
}

QString folder()
{
    return g_folder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/autosave")
                              : g_folder;
}

bool write(const doc::Document &document, const QString &projectPath, QString *error)
{
    QDir().mkpath(folder());
    const QString key = keyOf(projectPath);
    const QString file = QDir(folder()).filePath(key + QStringLiteral(".moth"));
    if (!saveProject(document, file, error))
        return false;
    QSaveFile info(QDir(folder()).filePath(key + QStringLiteral(".json")));
    if (!info.open(QIODevice::WriteOnly)) {
        if (error)
            *error = info.errorString();
        return false;
    }
    const QJsonObject o{{"path", projectPath.isEmpty() ? QString() : QFileInfo(projectPath).absoluteFilePath()},
                        {"when", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)}};
    info.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return info.commit();
}

void remove(const QString &projectPath)
{
    const QString key = keyOf(projectPath);
    QFile::remove(QDir(folder()).filePath(key + QStringLiteral(".moth")));
    QFile::remove(QDir(folder()).filePath(key + QStringLiteral(".json")));
}

static std::optional<Entry> readEntry(const QString &jsonPath)
{
    QFile in(jsonPath);
    if (!in.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(in.readAll()).object();
    if (o.isEmpty())
        return std::nullopt;
    Entry entry;
    entry.projectPath = o.value("path").toString();
    entry.name = nameOf(entry.projectPath);
    entry.file = QFileInfo(jsonPath).dir().filePath(QFileInfo(jsonPath).completeBaseName() + QStringLiteral(".moth"));
    entry.when = QDateTime::fromString(o.value("when").toString(), Qt::ISODateWithMs);
    if (!QFileInfo::exists(entry.file) || !entry.when.isValid())
        return std::nullopt;
    return entry;
}

std::optional<Entry> find(const QString &projectPath)
{
    return readEntry(QDir(folder()).filePath(keyOf(projectPath) + QStringLiteral(".json")));
}

QList<Entry> all()
{
    QList<Entry> entries;
    const QDir dir(folder());
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
        if (const auto entry = readEntry(dir.filePath(name)))
            entries << *entry;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.when < b.when; });
    return entries;
}

bool isStale(const Entry &entry)
{
    if (entry.projectPath.isEmpty())
        return false; // a project never saved: the copy is all there is of it
    const QFileInfo project(entry.projectPath);
    return project.exists() && project.lastModified() >= entry.when;
}

} // namespace mothion::io::autosave
