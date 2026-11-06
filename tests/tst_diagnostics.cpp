#include "diagnostics/CrashHandler.h"
#include "core/document/Document.h"
#include "io/ProjectFile.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace mothion;

class TestDiagnostics : public QObject
{
    Q_OBJECT
private slots:
    void logKeepsMessagesAndTheLastRuns()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        diagnostics::install(QStringLiteral("9.9"), dir.path());
        qWarning("something odd %d", 42);
        diagnostics::note(QStringLiteral("Opened a project"));
        QFile log(diagnostics::logFile());
        QVERIFY(log.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(log.readAll());
        QVERIFY(text.contains(QLatin1String("Mothion 9.9")));
        QVERIFY(text.contains(QLatin1String("[W] something odd 42")));
        QVERIFY(text.contains(QLatin1String("[N] Opened a project")));
        // A new run: this log becomes the previous one; only a few are kept.
        for (int i = 0; i < 5; ++i)
            diagnostics::install(QStringLiteral("9.9"), dir.path());
        QVERIFY(QFile::exists(QDir(diagnostics::logDirectory()).filePath(QStringLiteral("mothion.1.log"))));
        QVERIFY(QFile::exists(QDir(diagnostics::logDirectory()).filePath(QStringLiteral("mothion.2.log"))));
        QVERIFY(!QFile::exists(QDir(diagnostics::logDirectory()).filePath(QStringLiteral("mothion.3.log"))));
    }

    void aCrashLeavesAReportACopyOfTheProjectAndAMarker()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        diagnostics::install(QStringLiteral("9.9"), dir.path());
        QVERIFY(!diagnostics::takePreviousCrash().happened);
        doc::Document document(64, 48);
        diagnostics::setProjectPath(QStringLiteral("/somewhere/My Drawing.moth"));
        diagnostics::setEmergencySave([&document](const QString &path) { return io::saveProject(document, path, nullptr); });

        diagnostics::writeCrashReport("a test crash");

        const diagnostics::PreviousCrash crash = diagnostics::takePreviousCrash();
        QVERIFY(crash.happened);
        QVERIFY(QFile::exists(crash.report));
        QVERIFY(QFile::exists(crash.backup));
        QVERIFY(QFileInfo(crash.backup).fileName().startsWith(QLatin1String("My Drawing-backup-")));
        QString error;
        QVERIFY2(io::loadProject(crash.backup, &error), qPrintable(error)); // the copy opens
        QFile report(crash.report);
        QVERIFY(report.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(report.readAll());
        QVERIFY(text.contains(QLatin1String("a test crash")));
        QVERIFY(text.contains(QLatin1String("My Drawing.moth")));
#ifndef _WIN32
        QVERIFY(text.contains(QLatin1String("Stack of the crashing thread")));
#endif
        QVERIFY(!diagnostics::takePreviousCrash().happened); // told once
        diagnostics::setEmergencySave({});
    }

    void aCrashWithoutACopyStillLeavesTheReport()
    {
        QTemporaryDir dir;
        diagnostics::install(QStringLiteral("9.9"), dir.path());
        diagnostics::setEmergencySave([](const QString &) { return false; });
        diagnostics::writeCrashReport("no copy");
        const diagnostics::PreviousCrash crash = diagnostics::takePreviousCrash();
        QVERIFY(crash.happened);
        QVERIFY(QFile::exists(crash.report));
        QVERIFY(crash.backup.isEmpty());
        diagnostics::setEmergencySave({});
    }
};

QTEST_MAIN(TestDiagnostics)
#include "tst_diagnostics.moc"
