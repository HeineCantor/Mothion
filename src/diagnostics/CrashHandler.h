#pragma once

#include <QDateTime>
#include <QString>

#include <functional>

namespace mothion::diagnostics {

// What is left to understand a crash:
//  - a log (the Qt messages, with the time, and a few lines about what the program did) in the log directory,
//    the last three runs kept;
//  - when the program dies (a signal, an unhandled exception, abort), a report in the crash directory: on Linux
//    the reason and the stack of calls, on Windows a minidump and a short text;
//  - when possible, a copy of the project in the crash directory (the program may be too damaged to write it).
// The next start finds the marker the crash left and tells the user.

// Starts the log and puts the handlers in place. `baseDirectory` is where the log and crash directories go
// (default: the application's data directory).
void install(const QString &version, const QString &baseDirectory = {});

QString logDirectory();
QString crashDirectory();
QString logFile(); // the current run's log

// A line in the log, for what the program does (a project opened, saved...).
void note(const QString &what);

// The project being worked on, named in reports and in the backup's name.
void setProjectPath(const QString &path);
// Saves the project to `path`; called after a crash, once the report is written. Returns whether it worked.
void setEmergencySave(std::function<bool(const QString &path)> save);

// What the handlers do on a crash, without dying: the report, the backup and the marker. `reason` is written
// into the report. Meant for the handlers, and for the tests.
void writeCrashReport(const char *reason);

struct PreviousCrash
{
    bool happened = false;
    QString report;
    QString backup; // empty if there is none
    QDateTime when;
};
// The crash of an earlier run, if there was one; the marker is taken away, so it is told once.
PreviousCrash takePreviousCrash();

} // namespace mothion::diagnostics
