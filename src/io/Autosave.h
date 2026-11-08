#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace mothion::doc {
class Document;
}

namespace mothion::io::autosave {

// Copies of the projects being worked on, kept apart from the project files, so that what was done since the last
// save is not lost if the program or the machine dies. They live in the application data folder (autosave/): a .moth
// file and a small .json beside it that says which project it is a copy of, when it was made, and whether it was
// retired.
//
// Each write makes a new copy and keeps the last three of the project (kCopiesKept): a recovery that turns out to be
// older than hoped, or a save made over the wrong state, can still go back. The newest copy of a project that was not
// retired is the one offered for recovery at the next start; a project saved, closed on purpose, or whose copy was
// given up *retires* its copies instead of deleting them, and they stay as backups until they are older than
// kDaysKept days (File > Open backup lists them).
constexpr int kCopiesKept = 3;
constexpr int kDaysKept = 7;

// Where the copies are kept (default: the application data folder's autosave/). The tests use their own.
void setFolder(const QString &folder);
QString folder();

struct Entry
{
    QString projectPath; // the project it is a copy of ("" if it was not saved yet)
    QString name;        // to show: the project's name, or "Untitled"
    QString file;        // the copy
    QDateTime when;
    bool retired = false; // its project was saved or closed since: kept only as a backup
};

// Writes a new copy of `document`, a copy of the project at `projectPath`; the oldest of that project's copies go if
// there are more than kCopiesKept (or older than kDaysKept days).
bool write(const doc::Document &document, const QString &projectPath, QString *error = nullptr);
// Retires the project's copies: not offered for recovery any more, kept as backups.
void retire(const QString &projectPath);
// The newest copy of the project that is not retired.
std::optional<Entry> find(const QString &projectPath);
// What recovery offers: for each project, its newest copy if it is not retired; the oldest first.
QList<Entry> all();
// Every copy there is, retired or not, the newest first.
QList<Entry> backups();
// Takes away the copies over the limits (too many for a project, or too old). Done after each write, and at start.
void prune();

// Whether the copy is of no use for recovery: the project was saved after it was made (or it is a copy of a project
// that no longer has any file and is not unsaved either).
bool isStale(const Entry &entry);

} // namespace mothion::io::autosave
