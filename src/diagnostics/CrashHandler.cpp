#include "diagnostics/CrashHandler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QScreen>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <exception>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mothion::diagnostics {

namespace {

constexpr int kRunsKept = 3;
constexpr qint64 kLogLimit = 8 * 1024 * 1024; // a run's log stops growing here

QString g_logDirectory;
QString g_crashDirectory;
QString g_logPath;
QString g_version;

QMutex g_logMutex;
QFile g_log;
qint64 g_logSize = 0;
QtMessageHandler g_previousHandler = nullptr;

QMutex g_projectMutex;
QString g_projectPath;
std::function<bool(const QString &)> g_emergencySave;

// What the crash handlers use: plain buffers, ready before anything goes wrong (a handler cannot ask Qt).
constexpr int kPath = 1024;
constexpr int kReportPath = kPath + 64; // the directory and a file name
char g_markerPath[kPath];
char g_reportDirectory[kPath];
char g_versionText[64];
char g_projectText[kPath];
std::atomic<bool> g_crashing{false};

void copyText(char *to, int size, const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    std::snprintf(to, size_t(size), "%s", bytes.constData());
}

QString stamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

// --- The log ---------------------------------------------------------------------------------

void rotateLogs(const QString &directory)
{
    QDir dir(directory);
    dir.remove(QStringLiteral("mothion.%1.log").arg(kRunsKept - 1));
    for (int i = kRunsKept - 2; i >= 1; --i)
        QFile::rename(dir.filePath(QStringLiteral("mothion.%1.log").arg(i)),
                      dir.filePath(QStringLiteral("mothion.%1.log").arg(i + 1)));
    QFile::rename(dir.filePath(QStringLiteral("mothion.log")), dir.filePath(QStringLiteral("mothion.1.log")));
}

void writeLogLine(const QString &line)
{
    QMutexLocker lock(&g_logMutex);
    if (!g_log.isOpen() || g_logSize > kLogLimit)
        return;
    const QByteArray bytes = line.toUtf8() + '\n';
    g_log.write(bytes);
    g_log.flush(); // to the system at once: a crash a moment later must not take the line with it
    g_logSize += bytes.size();
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const char *level = type == QtDebugMsg ? "D" : type == QtInfoMsg ? "I" : type == QtWarningMsg ? "W" : type == QtCriticalMsg ? "C" : "F";
    QString where;
    if (context.file && type != QtDebugMsg && type != QtInfoMsg)
        where = QStringLiteral(" (%1:%2)").arg(QFileInfo(QString::fromUtf8(context.file)).fileName()).arg(context.line);
    writeLogLine(QStringLiteral("%1 [%2] %3%4%5")
                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), level,
                          context.category && std::strcmp(context.category, "default") != 0
                              ? QString::fromUtf8(context.category) + QStringLiteral(": ")
                              : QString(),
                          message, where));
    if (g_previousHandler)
        g_previousHandler(type, context, message); // still on the console
}

void describeSession()
{
    writeLogLine(QStringLiteral("Mothion %1, Qt %2 (built with %3)").arg(g_version, QString::fromLatin1(qVersion()),
                                                                          QStringLiteral(QT_VERSION_STR)));
    writeLogLine(QStringLiteral("%1, %2, %3").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
                                                  QSysInfo::kernelVersion()));
    if (QGuiApplication::instance()) {
        writeLogLine(QStringLiteral("Platform %1, %2 CPU threads").arg(QGuiApplication::platformName()).arg(QThread::idealThreadCount()));
        for (const QScreen *screen : QGuiApplication::screens())
            writeLogLine(QStringLiteral("Screen %1: %2x%3, ratio %4").arg(screen->name()).arg(screen->size().width())
                             .arg(screen->size().height()).arg(screen->devicePixelRatio()));
    }
}

void onTerminate()
{
    QString what = QStringLiteral("terminate called");
    if (const auto current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception &e) {
            what = QStringLiteral("unhandled exception: %1").arg(QString::fromUtf8(e.what()));
        } catch (...) {
            what = QStringLiteral("unhandled exception of an unknown type");
        }
    }
    writeLogLine(QStringLiteral("%1 [F] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), what));
    std::abort(); // the handler of SIGABRT writes the report
}

// --- The report ------------------------------------------------------------------------------

// fopen for a path in UTF-8: Windows' narrow fopen reads it in the ANSI code page, which does not hold every user name.
FILE *openFile(const char *utf8Path, const char *mode)
{
#ifdef _WIN32
    wchar_t widePath[kReportPath];
    wchar_t wideMode[8];
    if (MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, widePath, kReportPath) <= 0
        || MultiByteToWideChar(CP_UTF8, 0, mode, -1, wideMode, 8) <= 0)
        return nullptr;
    return _wfopen(widePath, wideMode);
#else
    return std::fopen(utf8Path, mode);
#endif
}

// The time as it is in the names of the report and the copy.
void currentStamp(char *text, size_t size)
{
    const time_t now = std::time(nullptr);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::strftime(text, size, "%Y-%m-%d_%H-%M-%S", &local);
}

// The part of the marker file that tells the next start where things are: a line per fact.
void markerAppend(const char *key, const char *value)
{
    FILE *file = openFile(g_markerPath, "ab");
    if (!file)
        return;
    std::fprintf(file, "%s=%s\n", key, value);
    std::fclose(file);
}

#ifndef _WIN32

void writeAll(int fd, const char *text)
{
    const size_t length = std::strlen(text);
    size_t done = 0;
    while (done < length) {
        const ssize_t n = ::write(fd, text + done, length - done);
        if (n <= 0)
            return;
        done += size_t(n);
    }
}

// Writes the report, and returns its path in `path`.
void writeReportFile(const char *reason, void *, char *path, size_t pathSize)
{
    char stampText[32];
    currentStamp(stampText, sizeof stampText);
    std::snprintf(path, pathSize, "%s/crash-%s.txt", g_reportDirectory, stampText);
    const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return;
    char line[3 * kPath];
    writeAll(fd, "Mothion crash report\n");
    std::snprintf(line, sizeof line, "Version: %s\nReason: %s\nProject: %s\nTime: %s\n\nStack of the crashing thread "
                                     "(addr2line -e mothion -f -C -p <offset> turns an offset into a place):\n",
                  g_versionText, reason, g_projectText[0] ? g_projectText : "(none)", stampText);
    writeAll(fd, line);
    void *frames[96];
    const int count = ::backtrace(frames, 96);
    ::backtrace_symbols_fd(frames, count, fd);
    writeAll(fd, "\nThe log of the run is next to the other runs' logs (see the log directory); its last lines come "
                 "before the crash.\n");
    ::close(fd);
}

#else

// "module.exe+0xOFFSET": where an address is, in a way that does not change from run to run (the modules are loaded
// at other addresses each time): the .pdb of the build says which function and line that offset is.
void writeAddress(FILE *file, DWORD64 address)
{
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module))
        GetModuleFileNameA(module, name, MAX_PATH);
    const char *base = std::strrchr(name, '\\');
    std::fprintf(file, "  %s+0x%llX  (at 0x%llX)\n", base ? base + 1 : name,
                 static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)),
                 static_cast<unsigned long long>(address));
}

// The stack of the crashing thread, from the registers the exception caught (the ones of this very place, for an
// abort), unwound with the tables every x64 module has: no symbols needed, the .pdb is for later.
void writeStack(FILE *file, EXCEPTION_POINTERS *exception)
{
#if defined(_M_X64) || defined(__x86_64__)
    CONTEXT context;
    if (exception && exception->ContextRecord)
        context = *exception->ContextRecord;
    else
        RtlCaptureContext(&context);
    for (int i = 0; i < 64 && context.Rip; ++i) {
        writeAddress(file, context.Rip);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!function) { // a leaf: the return address is at the top of the stack
            context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
            context.Rsp += 8;
        } else {
            void *handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                             &establisher, nullptr);
        }
    }
#else
    Q_UNUSED(file)
    Q_UNUSED(exception)
#endif
}

void writeReportFile(const char *reason, void *exceptionInfo, char *path, size_t pathSize)
{
    char stampText[32];
    currentStamp(stampText, sizeof stampText);
    std::snprintf(path, pathSize, "%s/crash-%s.txt", g_reportDirectory, stampText);
    FILE *file = openFile(path, "wb");
    if (!file)
        return;
    std::fprintf(file, "Mothion crash report\nVersion: %s\nReason: %s\nProject: %s\nTime: %s\n", g_versionText, reason,
                 g_projectText[0] ? g_projectText : "(none)", stampText);
    auto *exception = static_cast<EXCEPTION_POINTERS *>(exceptionInfo);
    if (exception && exception->ExceptionRecord) {
        const EXCEPTION_RECORD *record = exception->ExceptionRecord;
        std::fprintf(file, "\nWhere it happened:\n");
        writeAddress(file, reinterpret_cast<DWORD64>(record->ExceptionAddress));
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            const ULONG_PTR kind = record->ExceptionInformation[0];
            std::fprintf(file, "It %s the address 0x%llX.\n",
                         kind == 0 ? "read" : kind == 1 ? "wrote" : "ran code at",
                         static_cast<unsigned long long>(record->ExceptionInformation[1]));
        }
    }
    std::fprintf(file, "\nStack of the crashing thread (offsets in each module; the .pdb of this build turns them into "
                       "functions):\n");
    writeStack(file, exception);
    std::fprintf(file, "\nThe minidump next to this file (same name, .dmp) has all the threads and the memory; the log "
                       "of the run is among the other runs' logs.\n");
    std::fclose(file);
}

// The minidump, next to the text: Visual Studio or WinDbg open it (with the .pdb of the build).
void writeMiniDump(EXCEPTION_POINTERS *exception, const char *reportPath)
{
    wchar_t path[kReportPath];
    wchar_t name[kReportPath];
    const int n = MultiByteToWideChar(CP_UTF8, 0, reportPath, -1, name, kReportPath);
    if (n <= 0)
        return;
    wchar_t *dot = wcsrchr(name, L'.');
    if (dot)
        wcscpy_s(dot, size_t(kReportPath - (dot - name)), L".dmp");
    wcscpy_s(path, kReportPath, name);
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    MINIDUMP_EXCEPTION_INFORMATION info;
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo),
                      exception ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

#endif

void emergencyBackup(const char *reportPath)
{
    std::function<bool(const QString &)> save;
    QString project;
    {
        // Not a lock to wait for: the crash may have happened while it was held.
        if (!g_projectMutex.tryLock())
            return;
        save = g_emergencySave;
        project = g_projectPath;
        g_projectMutex.unlock();
    }
    if (!save)
        return;
    const QString base = project.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(project).completeBaseName();
    const QString target = QFileInfo(QString::fromUtf8(reportPath)).dir().filePath(
        QStringLiteral("%1-backup-%2.moth").arg(base, stamp()));
    if (save(target))
        markerAppend("backup", target.toUtf8().constData());
}

void crash(const char *reason, void *exceptionInfo)
{
    if (g_crashing.exchange(true))
        return; // crashing again while handling a crash: the report is written, nothing more to try
    char reportPath[kReportPath];
    reportPath[0] = '\0';
    writeReportFile(reason, exceptionInfo, reportPath, sizeof reportPath);
    if (reportPath[0])
        markerAppend("report", reportPath);
#ifdef _WIN32
    writeMiniDump(static_cast<EXCEPTION_POINTERS *>(exceptionInfo), reportPath);
#else
    Q_UNUSED(exceptionInfo)
#endif
    writeLogLine(QStringLiteral("%1 [F] crash: %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), QString::fromUtf8(reason)));
#ifndef _WIN32
    alarm(20); // the copy of the project may hang in a program this damaged: the default action ends it then
#endif
    if (reportPath[0])
        emergencyBackup(reportPath);
}

#ifdef _WIN32

LONG WINAPI onException(EXCEPTION_POINTERS *info)
{
    char reason[96];
    std::snprintf(reason, sizeof reason, "exception 0x%08lX at %p", info->ExceptionRecord->ExceptionCode,
                  info->ExceptionRecord->ExceptionAddress);
    crash(reason, info);
    return EXCEPTION_CONTINUE_SEARCH; // Windows' own handling goes on
}

void onSignal(int signal)
{
    crash(signal == SIGABRT ? "abort (a failed check or an unhandled exception)" : "signal", nullptr);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void installHandlers()
{
    SetUnhandledExceptionFilter(onException);
    std::signal(SIGABRT, onSignal);
}

#else

void onSignal(int signal, siginfo_t *info, void *)
{
    char reason[96];
    const char *name = signal == SIGSEGV ? "SIGSEGV (invalid memory access)"
                       : signal == SIGABRT ? "SIGABRT (abort: a failed check or an unhandled exception)"
                       : signal == SIGFPE ? "SIGFPE (arithmetic error)"
                       : signal == SIGILL ? "SIGILL (illegal instruction)"
                       : signal == SIGBUS ? "SIGBUS (bad memory access)" : "signal";
    std::snprintf(reason, sizeof reason, "%s, address %p", name, info ? info->si_addr : nullptr);
    crash(reason, nullptr);
    ::signal(signal, SIG_DFL); // then the system does what it would have done (core dump, its own report)
    ::raise(signal);
}

void installHandlers()
{
    // A stack of its own: a crash from a stack overflow still has room to write.
    static constexpr size_t kStack = 2 * 1024 * 1024;
    stack_t stack;
    stack.ss_sp = std::malloc(kStack);
    stack.ss_size = kStack;
    stack.ss_flags = 0;
    if (stack.ss_sp)
        sigaltstack(&stack, nullptr);
    void *warm[4];
    ::backtrace(warm, 4); // the first call loads the library: not in the handler
    struct sigaction action;
    std::memset(&action, 0, sizeof action);
    action.sa_sigaction = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    for (const int signal : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
        sigaction(signal, &action, nullptr);
}

#endif

} // namespace

void install(const QString &version, const QString &baseDirectory)
{
    const QString base = baseDirectory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                                 : baseDirectory;
    g_version = version;
    g_logDirectory = QDir(base).filePath(QStringLiteral("logs"));
    g_crashDirectory = QDir(base).filePath(QStringLiteral("crashes"));
    QDir().mkpath(g_logDirectory);
    QDir().mkpath(g_crashDirectory);

    {
        QMutexLocker lock(&g_logMutex);
        if (g_log.isOpen())
            g_log.close();
        rotateLogs(g_logDirectory);
        g_logPath = QDir(g_logDirectory).filePath(QStringLiteral("mothion.log"));
        g_log.setFileName(g_logPath);
        g_logSize = 0;
        if (!g_log.open(QIODevice::WriteOnly | QIODevice::Truncate))
            std::fprintf(stderr, "Mothion: the log %s cannot be written\n", qPrintable(g_logPath));
    }
    if (!g_previousHandler)
        g_previousHandler = qInstallMessageHandler(messageHandler);
    describeSession();

    copyText(g_markerPath, kPath, QDir(g_crashDirectory).filePath(QStringLiteral("pending.txt")));
    copyText(g_reportDirectory, kPath, g_crashDirectory);
    copyText(g_versionText, int(sizeof g_versionText), version);
    g_crashing = false;
    std::set_terminate(onTerminate);
    static bool handlersInstalled = false;
    if (!handlersInstalled) { // once per process; the tests call install() more than once
        handlersInstalled = true;
        installHandlers();
    }
}

QString logDirectory() { return g_logDirectory; }
QString crashDirectory() { return g_crashDirectory; }
QString logFile() { return g_logPath; }

void note(const QString &what)
{
    writeLogLine(QStringLiteral("%1 [N] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")), what));
}

void setProjectPath(const QString &path)
{
    {
        QMutexLocker lock(&g_projectMutex);
        g_projectPath = path;
    }
    copyText(g_projectText, kPath, path);
}

void setEmergencySave(std::function<bool(const QString &)> save)
{
    QMutexLocker lock(&g_projectMutex);
    g_emergencySave = std::move(save);
}

void writeCrashReport(const char *reason)
{
    g_crashing = false;
    crash(reason, nullptr);
    g_crashing = false;
}

PreviousCrash takePreviousCrash()
{
    PreviousCrash crash;
    QFile marker(QDir(g_crashDirectory).filePath(QStringLiteral("pending.txt")));
    if (!marker.open(QIODevice::ReadOnly))
        return crash;
    const QFileInfo info(marker);
    crash.when = info.lastModified();
    while (!marker.atEnd()) {
        const QString line = QString::fromUtf8(marker.readLine()).trimmed();
        if (line.startsWith(QLatin1String("report=")))
            crash.report = line.mid(7);
        else if (line.startsWith(QLatin1String("backup=")))
            crash.backup = line.mid(7);
    }
    marker.close();
    marker.remove();
    crash.happened = true;
    return crash;
}

} // namespace mothion::diagnostics
