#include "core/document/Document.h"
#include "document/DocumentController.h"
#include "io/Autosave.h"
#include "io/ProjectFile.h"
#include "ui/MainWindow.h"

#include <QFile>
#include <QAbstractButton>
#include <QApplication>
#include <QImage>
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
        // The next write replaces the copy: one for each project.
        QVERIFY(io::autosave::write(document, project));
        QCOMPARE(io::autosave::all().size(), 2);
        io::autosave::remove(project);
        QVERIFY(!io::autosave::find(project));
        QCOMPARE(io::autosave::all().size(), 1);
        io::autosave::remove(QString());
        QVERIFY(io::autosave::all().isEmpty());
        QVERIFY(QDir(io::autosave::folder()).entryList(QDir::Files).isEmpty()); // no file left behind
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
