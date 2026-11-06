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
// save is not lost if the program or the machine dies. One copy for each project (by its path; "" is a project not
// saved yet): the next write replaces it. They live in the application data folder (autosave/): a .moth file and
// a small .json beside it that says which project it is a copy of, and when it was made.

// Where the copies are kept (default: the application data folder's autosave/). The tests use their own.
void setFolder(const QString &folder);
QString folder();

struct Entry
{
    QString projectPath; // the project it is a copy of ("" if it was not saved yet)
    QString name;        // to show: the project's name, or "Untitled"
    QString file;        // the copy
    QDateTime when;
};

// Writes the copy of `document`, a copy of the project at `projectPath`.
bool write(const doc::Document &document, const QString &projectPath, QString *error = nullptr);
// Takes the copy away (the project was saved, or its changes were given up).
void remove(const QString &projectPath);
std::optional<Entry> find(const QString &projectPath);
// Every copy there is, the oldest first.
QList<Entry> all();

// Whether the copy is of no use: the project was saved after it was made (or it is a copy of a project that no
// longer has any file and is not unsaved either).
bool isStale(const Entry &entry);

} // namespace mothion::io::autosave
