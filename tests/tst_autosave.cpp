#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "io/Autosave.h"
#include "io/ProjectFile.h"
#include "ui/BackupDialog.h"
#include "ui/MainWindow.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractButton>
#include <QApplication>
#include <QImage>
#include <QTreeWidget>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QtTest>

using namespace mothion;

class TestAutosave : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        io::autosave::setFolder(m_dir->filePath(QStringLiteral("autosave")));
    }
    void cleanup() { io::autosave::setFolder(QString()); }

    void aCopyIsWrittenFoundAndTakenAway()
    {
        doc::Document document(64, 48);
        const QString project = m_dir->filePath(QStringLiteral("walk.moth"));
        QVERIFY(io::autosave::all().isEmpty());
        QVERIFY(io::autosave::write(document, project));
        QVERIFY(io::autosave::write(document, QString())); // a project not saved yet
        QCOMPARE(io::autosave::all().size(), 2);
        const auto entry = io::autosave::find(project);
        QVERIFY(entry);
        QCOMPARE(entry->name, QStringLiteral("walk"));
        QCOMPARE(QFileInfo(entry->projectPath).absoluteFilePath(), QFileInfo(project).absoluteFilePath());
        QString error;
        QVERIFY2(io::loadProject(entry->file, &error), qPrintable(error)); // the copy opens
        QCOMPARE(io::autosave::find(QString())->name, QStringLiteral("Untitled"));
        io::autosave::retire(project);
        QVERIFY(!io::autosave::find(project)); // not offered for recovery any more...
        QCOMPARE(io::autosave::all().size(), 1);
        io::autosave::retire(QString());
        QVERIFY(io::autosave::all().isEmpty());
        QCOMPARE(io::autosave::backups().size(), 2); // ...but kept
        QVERIFY(io::autosave::backups().first().retired);
    }

    // The last three copies of a project are kept, the oldest go; one project's copies do not push out another's.
    void theLastThreeCopiesOfAProjectAreKept()
    {
        doc::Document document(64, 48);
        const QString project = m_dir->filePath(QStringLiteral("walk.moth"));
        QVERIFY(io::autosave::write(document, QString())); // another project's
        for (int i = 0; i < 5; ++i) {
            QVERIFY(io::autosave::write(document, project));
            QTest::qWait(5);
        }
        QList<io::autosave::Entry> mine;
        for (const auto &entry : io::autosave::backups())
            if (!entry.projectPath.isEmpty())
                mine << entry;
        QCOMPARE(mine.size(), io::autosave::kCopiesKept);
        QCOMPARE(io::autosave::backups().size(), io::autosave::kCopiesKept + 1); // and the other project's one
        QCOMPARE(io::autosave::find(project)->file, mine.first().file); // the newest is the one to recover
        QCOMPARE(io::autosave::all().size(), 2); // one for each project
        for (const auto &entry : mine)
            QVERIFY(QFileInfo::exists(entry.file));
        // The files: a copy and its note, for each (4 copies kept in all).
        QCOMPARE(QDir(io::autosave::folder()).entryList(QDir::Files).size(), 2 * (io::autosave::kCopiesKept + 1));
    }

    // Saved or closed: the copies are no longer offered, and go once they are old; the one that still waits to be
    // recovered stays however old.
    void retiredCopiesGoWhenOldButAWaitingOneStays()
    {
        doc::Document document(64, 48);
        const QString saved = m_dir->filePath(QStringLiteral("saved.moth"));
        const QString waiting = m_dir->filePath(QStringLiteral("waiting.moth"));
        QVERIFY(io::autosave::write(document, saved));
        QVERIFY(io::autosave::write(document, waiting));
        io::autosave::retire(saved);
        auto age = [](const QString &project, int days) {
            for (const io::autosave::Entry &entry : io::autosave::backups()) {
                if (QFileInfo(entry.projectPath).absoluteFilePath() != QFileInfo(project).absoluteFilePath())
                    continue;
                QFile f(QFileInfo(entry.file).dir().filePath(QFileInfo(entry.file).completeBaseName() + QStringLiteral(".json")));
                QVERIFY(f.open(QIODevice::ReadOnly));
                QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
                f.close();
                o["when"] = QDateTime::currentDateTime().addDays(-days).toString(Qt::ISODateWithMs);
                QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
                f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
            }
        };
        age(saved, io::autosave::kDaysKept - 1);
        age(waiting, io::autosave::kDaysKept + 30);
        io::autosave::prune();
        QCOMPARE(io::autosave::backups().size(), 2); // a retired one not old enough, a waiting one that is very old
        age(saved, io::autosave::kDaysKept + 1);
        io::autosave::prune();
        QCOMPARE(io::autosave::backups().size(), 1);
        QCOMPARE(io::autosave::backups().first().projectPath, QFileInfo(waiting).absoluteFilePath());
        QVERIFY(io::autosave::find(waiting)); // still offered
        io::autosave::retire(waiting);
        io::autosave::prune();
        QVERIFY(io::autosave::backups().isEmpty()); // retired and old: gone
        QVERIFY(QDir(io::autosave::folder()).entryList(QDir::Files).isEmpty());
    }

    void aCopyIsStaleOnceTheProjectIsSavedAfterIt()
    {
        doc::Document document(64, 48);
        const QString project = m_dir->filePath(QStringLiteral("walk.moth"));
        QVERIFY(io::autosave::write(document, project));
        auto entry = *io::autosave::find(project);
        QVERIFY(!io::autosave::isStale(entry)); // the project has no file: the copy is all there is
        QVERIFY(io::saveProject(document, project, nullptr)); // saved after the copy
        QVERIFY(io::autosave::isStale(entry));
        entry.projectPath.clear(); // an unsaved project's copy is never stale
        QVERIFY(!io::autosave::isStale(entry));
    }

    void brokenFilesAreIgnored()
    {
        QDir().mkpath(io::autosave::folder());
        QFile junk(QDir(io::autosave::folder()).filePath(QStringLiteral("junk.json")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("{ not json");
        junk.close();
        QFile orphan(QDir(io::autosave::folder()).filePath(QStringLiteral("orphan.json"))); // a note without its copy
        QVERIFY(orphan.open(QIODevice::WriteOnly));
        orphan.write("{\"path\":\"\",\"when\":\"2026-01-01T00:00:00.000\"}");
        orphan.close();
        QVERIFY(io::autosave::all().isEmpty());
    }

    void theWindowKeepsACopyOnlyOfUnsavedWork()
    {
        ui::MainWindow window;
        auto *controller = window.findChild<document::DocumentController *>();
        QVERIFY(controller);
        QVERIFY(!window.autosaveNow()); // nothing was done
        QImage image(20, 20, QImage::Format_ARGB32);
        image.fill(Qt::red);
        controller->importImage(image, QStringLiteral("x"));
        QVERIFY(window.autosaveNow());
        QVERIFY(io::autosave::find(QString())); // untitled
        QVERIFY(!window.autosaveNow());          // no new work since the last copy

        // Saved: the copies are of no use any more.
        const QString project = m_dir->filePath(QStringLiteral("saved.moth"));
        QVERIFY(window.saveTo(project));
        QVERIFY(io::autosave::all().isEmpty());
        controller->importImage(image, QStringLiteral("y"));
        QVERIFY(window.autosaveNow());
        QVERIFY(io::autosave::find(project)); // a copy of the saved project, now
        QVERIFY(!io::autosave::find(QString()));
    }

    // The copies are kept after a save or a close, and one can be opened as a new project: the way back from a
    // recovery that was older than the work, or a save made over the wrong state.
    void aBackupOpensAsANewProjectAndNeverOverTheOriginal()
    {
        const QString project = m_dir->filePath(QStringLiteral("shot.moth"));
        QImage image(20, 20, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        ui::MainWindow window;
        auto *controller = window.findChild<document::DocumentController *>();
        QVERIFY(window.saveTo(project)); // the empty project
        controller->importImage(image, QStringLiteral("one"));
        QVERIFY(window.autosaveNow());
        QTest::qWait(5);
        controller->importImage(image, QStringLiteral("two"));
        QVERIFY(window.autosaveNow());
        QVERIFY(window.saveTo(project)); // saved: the copies are retired, not gone
        QVERIFY(io::autosave::all().isEmpty());
        const auto copies = io::autosave::backups();
        QCOMPARE(copies.size(), 2);
        QVERIFY(copies.first().retired);

        const qint64 sizeBefore = QFileInfo(project).size();
        // The older copy: one cel more than the empty project, one less than the saved one.
        QVERIFY(window.openBackupCopy(copies.last()));
        QCOMPARE(controller->document().celCount(), 2);
        QVERIFY(controller->isModified());     // it is work not saved
        QVERIFY(window.windowTitle().startsWith(QStringLiteral("Untitled"))); // not the project's name
        QCOMPARE(QFileInfo(project).size(), sizeBefore); // the project's file was not touched
        QVERIFY(window.autosaveNow()); // and the opened copy is kept like any work not saved
        QVERIFY(io::autosave::find(QString()));
    }

    void theBackupWindowListsTheCopiesNewestFirst()
    {
        doc::Document document(64, 48);
        const QString project = m_dir->filePath(QStringLiteral("walk.moth"));
        QVERIFY(io::autosave::write(document, project));
        QTest::qWait(5);
        QVERIFY(io::autosave::write(document, project));
        io::autosave::retire(project);
        QTest::qWait(5);
        QVERIFY(io::autosave::write(document, QString()));
        ui::BackupDialog dialog(io::autosave::backups());
        auto *list = dialog.findChild<QTreeWidget *>(QStringLiteral("backups"));
        QVERIFY(list);
        QCOMPARE(list->topLevelItemCount(), 3);
        QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("Untitled")); // the newest
        QCOMPARE(list->topLevelItem(1)->text(0), QStringLiteral("walk"));
        QVERIFY(list->topLevelItem(1)->text(2) != list->topLevelItem(0)->text(2)); // retired, and not
        QCOMPARE(dialog.selected().name, QStringLiteral("Untitled")); // the first is chosen at once
        list->setCurrentItem(list->topLevelItem(2));
        QCOMPARE(dialog.selected().name, QStringLiteral("walk"));
    }

    void anAutosaveIsRecoveredAsUnsavedWork()
    {
        QString project = m_dir->filePath(QStringLiteral("lost.moth"));
        {
            ui::MainWindow window;
            auto *controller = window.findChild<document::DocumentController *>();
            QImage image(20, 20, QImage::Format_ARGB32);
            image.fill(Qt::green);
            QVERIFY(window.saveTo(project)); // saved without the drawing...
            controller->importImage(image, QStringLiteral("drawing")); // ...which is lost by a crash
            QVERIFY(window.autosaveNow());
        }
        // The window is gone without closing properly: no discard. A new run offers the copy.
        QTimer::singleShot(200, this, [] {
            for (QWidget *top : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(top))
                    for (QAbstractButton *button : box->buttons())
                        if (button->text() == QStringLiteral("Recover"))
                            button->click();
        });
        ui::MainWindow again;
        again.checkRecovery();
        auto *controller = again.findChild<document::DocumentController *>();
        QVERIFY(controller->isModified()); // not what the project's file holds
        QCOMPARE(controller->document().celCount(), 2);
        QVERIFY(io::autosave::find(project)); // kept until the project is saved
        QVERIFY(again.saveTo(project));
        QVERIFY(!io::autosave::find(project));
    }

    void aStaleCopyIsDroppedAtStartWithoutAsking()
    {
        const QString project = m_dir->filePath(QStringLiteral("old.moth"));
        doc::Document document(64, 48);
        QVERIFY(io::autosave::write(document, project));
        QTest::qWait(20);
        QVERIFY(io::saveProject(document, project, nullptr)); // saved later than the copy
        ui::MainWindow window;
        window.checkRecovery(); // would block on a question if it asked
        QVERIFY(io::autosave::all().isEmpty());
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
};

QTEST_MAIN(TestAutosave)
#include "tst_autosave.moc"
