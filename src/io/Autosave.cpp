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

namespace {

// What a note says about a copy.
struct Note
{
    QString projectPath;
    QDateTime when;
    bool retired = false;
};

std::optional<Entry> readEntry(const QString &jsonPath)
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
    entry.retired = o.value("retired").toBool();
    if (!QFileInfo::exists(entry.file) || !entry.when.isValid())
        return std::nullopt;
    return entry;
}

QString noteOf(const Entry &entry)
{
    return QFileInfo(entry.file).dir().filePath(QFileInfo(entry.file).completeBaseName() + QStringLiteral(".json"));
}

bool writeNote(const QString &jsonPath, const Entry &entry)
{
    QSaveFile info(jsonPath);
    if (!info.open(QIODevice::WriteOnly))
        return false;
    const QJsonObject o{{"path", entry.projectPath.isEmpty() ? QString() : QFileInfo(entry.projectPath).absoluteFilePath()},
                        {"when", entry.when.toString(Qt::ISODateWithMs)},
                        {"retired", entry.retired}};
    info.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return info.commit();
}

bool sameProject(const QString &a, const QString &b)
{
    return a.isEmpty() ? b.isEmpty() : !b.isEmpty() && QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
}

void erase(const Entry &entry)
{
    QFile::remove(entry.file);
    QFile::remove(noteOf(entry));
}

} // namespace

QList<Entry> backups()
{
    QList<Entry> entries;
    const QDir dir(folder());
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
        if (const auto entry = readEntry(dir.filePath(name)))
            entries << *entry;
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.when > b.when; });
    return entries;
}

void prune()
{
    const QDateTime oldest = QDateTime::currentDateTime().addDays(-kDaysKept);
    QList<QString> seen; // the projects met so far, as the newest comes first: how many copies each has
    QList<int> counts;
    for (const Entry &entry : backups()) {
        const QString key = entry.projectPath.isEmpty() ? QString() : QFileInfo(entry.projectPath).absoluteFilePath();
        int index = int(seen.indexOf(key));
        if (index < 0) {
            seen << key;
            counts << 0;
            index = int(seen.size()) - 1;
        }
        // The newest of a project that is still waiting to be recovered stays, however old: it is all there is of it.
        const bool newest = counts[index] == 0;
        if (++counts[index] > kCopiesKept || (entry.when < oldest && !(newest && !entry.retired)))
            erase(entry);
    }
    // Notes without their copy, and copies without their note, are of no use.
    const QDir dir(folder());
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files))
        if (!QFileInfo::exists(dir.filePath(QFileInfo(name).completeBaseName() + QStringLiteral(".moth"))))
            QFile::remove(dir.filePath(name));
    for (const QString &name : dir.entryList({QStringLiteral("*.moth")}, QDir::Files))
        if (!QFileInfo::exists(dir.filePath(QFileInfo(name).completeBaseName() + QStringLiteral(".json"))))
            QFile::remove(dir.filePath(name));
}

bool write(const doc::Document &document, const QString &projectPath, QString *error)
{
    QDir().mkpath(folder());
    // A name of its own for each copy: the time, to the millisecond (and a number if two come in the same one).
    QString file;
    for (int n = 0; n < 1000; ++n) {
        file = QDir(folder()).filePath(QStringLiteral("%1-%2%3.moth")
                                           .arg(keyOf(projectPath))
                                           .arg(QDateTime::currentMSecsSinceEpoch())
                                           .arg(n == 0 ? QString() : QStringLiteral("-%1").arg(n)));
        if (!QFileInfo::exists(file))
            break;
    }
    if (!saveProject(document, file, error))
        return false;
    Entry entry;
    entry.projectPath = projectPath;
    entry.file = file;
    entry.when = QDateTime::currentDateTime();
    if (!writeNote(noteOf(entry), entry)) {
        QFile::remove(file);
        if (error)
            *error = QObject::tr("Cannot write the note of the copy");
        return false;
    }
    prune();
    return true;
}

void retire(const QString &projectPath)
{
    for (Entry entry : backups()) {
        if (entry.retired || !sameProject(entry.projectPath, projectPath))
            continue;
        entry.retired = true;
        writeNote(noteOf(entry), entry);
    }
}

std::optional<Entry> find(const QString &projectPath)
{
    for (const Entry &entry : backups()) // newest first
        if (sameProject(entry.projectPath, projectPath))
            return entry.retired ? std::nullopt : std::optional<Entry>(entry);
    return std::nullopt;
}

QList<Entry> all()
{
    QList<Entry> entries;
    QStringList seen;
    for (const Entry &entry : backups()) { // newest first: the first of each project is its newest
        const QString key = entry.projectPath.isEmpty() ? QString() : QFileInfo(entry.projectPath).absoluteFilePath();
        if (seen.contains(key))
            continue;
        seen << key;
        if (!entry.retired)
            entries << entry;
    }
    std::reverse(entries.begin(), entries.end());
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
