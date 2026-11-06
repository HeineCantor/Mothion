#include "tools/ColorSetLibrary.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace mothion::tools;

class TestColorSets : public QObject
{
    Q_OBJECT
private slots:
    void startsWithTheBuiltInSetOnly()
    {
        QTemporaryDir dir;
        ColorSetLibrary library(dir.path());
        const QList<ColorSet> sets = library.sets();
        QCOMPARE(sets.size(), 1);
        QCOMPARE(sets[0].id, ColorSetLibrary::kDefaultId);
        QVERIFY(sets[0].builtIn);
        QCOMPARE(sets[0].swatches.size(), 48);
        QCOMPARE(library.activeId(), ColorSetLibrary::kDefaultId);
    }

    void theBuiltInSetCannotBeChanged()
    {
        QTemporaryDir dir;
        ColorSetLibrary library(dir.path());
        const QString id = ColorSetLibrary::kDefaultId;
        QVERIFY(!library.addColor(id, Qt::red));
        QVERIFY(!library.setColor(id, 0, Qt::red));
        QVERIFY(!library.removeColor(id, 0));
        QVERIFY(!library.moveColor(id, 0, 1));
        QVERIFY(!library.rename(id, QStringLiteral("x")));
        QVERIFY(!library.remove(id));
        QCOMPARE(library.set(id)->swatches.size(), 48);
        // Duplicated, it can be changed.
        const QString copy = library.duplicate(id);
        QVERIFY(!copy.isEmpty());
        QVERIFY(!library.set(copy)->builtIn);
        QCOMPARE(library.set(copy)->swatches, library.set(id)->swatches);
        QVERIFY(library.addColor(copy, Qt::red));
        QCOMPARE(library.set(copy)->swatches.size(), 49);
    }

    void colorsAreAddedChangedNamedMovedAndRemoved()
    {
        QTemporaryDir dir;
        ColorSetLibrary library(dir.path());
        const QString id = library.create(QStringLiteral("Skin"));
        QVERIFY(!id.isEmpty());
        QSignalSpy changed(&library, &ColorSetLibrary::setChanged);
        QVERIFY(library.addColor(id, QColor(255, 0, 0), QStringLiteral("Red")));
        QVERIFY(library.addColor(id, QColor(0, 255, 0)));
        QVERIFY(library.addColor(id, QColor(0, 0, 255, 128), QStringLiteral("Glass")));
        QCOMPARE(changed.count(), 3);
        QCOMPARE(changed.last().first().toString(), id);
        QVERIFY(library.setColor(id, 1, QColor(10, 20, 30)));
        QVERIFY(library.setColorName(id, 1, QStringLiteral(" Slate ")));
        QCOMPARE(library.set(id)->swatches[1].name, QStringLiteral("Slate")); // trimmed, and the color kept
        QCOMPARE(library.set(id)->swatches[1].color, QColor(10, 20, 30));
        QVERIFY(library.moveColor(id, 0, 2));
        QCOMPARE(library.set(id)->swatches[2].name, QStringLiteral("Red"));
        QCOMPARE(library.set(id)->swatches[0].name, QStringLiteral("Slate"));
        QVERIFY(library.removeColor(id, 0));
        QCOMPARE(library.set(id)->swatches.size(), 2);
        QVERIFY(!library.setColor(id, 5, Qt::red)); // out of range
        QVERIFY(!library.removeColor(id, -1));
        QVERIFY(!library.addColor(id, QColor())); // not a color
    }

    void setsAreKeptBetweenRuns()
    {
        QTemporaryDir dir;
        QString id;
        {
            ColorSetLibrary library(dir.path());
            id = library.create(QStringLiteral("Night"));
            library.addColor(id, QColor(1, 2, 3, 200), QStringLiteral("Deep"));
            library.addColor(id, QColor(40, 50, 60));
            library.setActive(id);
        }
        ColorSetLibrary again(dir.path());
        QCOMPARE(again.sets().size(), 2);
        const ColorSet *set = again.set(id);
        QVERIFY(set);
        QCOMPARE(set->name, QStringLiteral("Night"));
        QCOMPARE(set->swatches.size(), 2);
        QCOMPARE(set->swatches[0].color, QColor(1, 2, 3, 200)); // alpha too
        QCOMPARE(set->swatches[0].name, QStringLiteral("Deep"));
        QVERIFY(set->swatches[1].name.isEmpty());
        QCOMPARE(again.activeId(), id); // and the active one
    }

    void namesAreUniqueAndSetsAreOrderedByName()
    {
        QTemporaryDir dir;
        ColorSetLibrary library(dir.path());
        const QString b = library.create(QStringLiteral("beta"));
        const QString a = library.create(QStringLiteral("Alpha"));
        const QString again = library.create(QStringLiteral("ALPHA"));
        QCOMPARE(library.set(again)->name, QStringLiteral("ALPHA 2"));
        QStringList names;
        for (const ColorSet &s : library.sets())
            names << s.name;
        QCOMPARE(names, (QStringList{QStringLiteral("Default"), QStringLiteral("Alpha"), QStringLiteral("ALPHA 2"),
                                     QStringLiteral("beta")}));
        QVERIFY(library.rename(b, QStringLiteral("Gamma")));
        QCOMPARE(library.sets().last().name, QStringLiteral("Gamma"));
        QVERIFY(!library.rename(a, QStringLiteral("  "))); // an empty name is refused
        QVERIFY(library.create(QString()).size() > 0);       // no name: a default one
    }

    void removingASetFallsBackToTheDefault()
    {
        QTemporaryDir dir;
        ColorSetLibrary library(dir.path());
        const QString id = library.create(QStringLiteral("Temp"));
        library.setActive(id);
        QSignalSpy sets(&library, &ColorSetLibrary::setsChanged);
        QSignalSpy active(&library, &ColorSetLibrary::activeChanged);
        QVERIFY(library.remove(id));
        QCOMPARE(sets.count(), 1);
        QCOMPARE(active.count(), 1);
        QCOMPARE(library.activeId(), ColorSetLibrary::kDefaultId);
        QVERIFY(!library.set(id));
        QVERIFY(!QFile::exists(dir.filePath(id + QStringLiteral(".json")))); // and its file is gone
        ColorSetLibrary again(dir.path());
        QCOMPARE(again.sets().size(), 1);
        QCOMPARE(again.activeId(), ColorSetLibrary::kDefaultId);
    }

    void foreignAndBrokenFilesAreIgnored()
    {
        QTemporaryDir dir;
        QFile junk(dir.filePath(QStringLiteral("junk.json")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("not json at all");
        junk.close();
        QFile other(dir.filePath(QStringLiteral("other.json")));
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write("{\"something\": 1}");
        other.close();
        ColorSetLibrary library(dir.path());
        QCOMPARE(library.sets().size(), 1);
    }
};

QTEST_MAIN(TestColorSets)
#include "tst_colorsets.moc"
