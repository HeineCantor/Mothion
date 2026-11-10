#include "io/BlenderAddOn.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace mothion::io {

QString findBlender(const QString &preferred)
{
    if (!preferred.isEmpty() && QFileInfo(preferred).isExecutable())
        return preferred;
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("blender.exe");
#else
    const QString name = QStringLiteral("blender");
#endif
    if (const QString found = QStandardPaths::findExecutable(name); !found.isEmpty())
        return found;
    // Where the installers put it, when it is not on the search path.
    QStringList places;
#ifdef Q_OS_WIN
    for (const QString &base : {QStringLiteral("C:/Program Files/Blender Foundation"),
                                QStringLiteral("C:/Program Files (x86)/Blender Foundation")}) {
        const QStringList versions = QDir(base).entryList({QStringLiteral("Blender*")}, QDir::Dirs, QDir::Name | QDir::Reversed);
        for (const QString &version : versions)
            places << base + QLatin1Char('/') + version + QStringLiteral("/blender.exe");
    }
#elif defined(Q_OS_MACOS)
    places << QStringLiteral("/Applications/Blender.app/Contents/MacOS/Blender");
#else
    places << QStringLiteral("/usr/bin/blender") << QStringLiteral("/usr/local/bin/blender")
           << QStringLiteral("/snap/bin/blender") << QDir::homePath() + QStringLiteral("/.local/bin/blender");
#endif
    for (const QString &place : places)
        if (QFileInfo(place).isExecutable())
            return place;
    return {};
}

bool installBlenderAddOn(const QString &blender, QString *message)
{
    auto say = [message](const QString &text) {
        if (message)
            *message = text;
    };
    QTemporaryDir dir;
    QFile resource(QStringLiteral(":/blender/mothion_link.zip"));
    if (!dir.isValid() || !resource.open(QIODevice::ReadOnly)) {
        say(QObject::tr("The add-on is not in this version of the program."));
        return false;
    }
    const QString zip = dir.filePath(QStringLiteral("mothion_link.zip"));
    QFile out(zip);
    if (!out.open(QIODevice::WriteOnly) || out.write(resource.readAll()) < 0) {
        say(QObject::tr("The add-on could not be written to a temporary folder."));
        return false;
    }
    out.close();
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(blender, {QStringLiteral("--command"), QStringLiteral("extension"), QStringLiteral("install-file"),
                            QStringLiteral("-r"), QStringLiteral("user_default"), QStringLiteral("--enable"), zip});
    if (!process.waitForStarted(10000) || !process.waitForFinished(120000)) {
        say(QObject::tr("Blender did not install the add-on."));
        process.kill();
        return false;
    }
    const QString output = QString::fromUtf8(process.readAll());
    if (process.exitCode() != 0 || !output.contains(QLatin1String("Installed"))) {
        say(QObject::tr("Blender did not install the add-on:\n%1").arg(output.right(600)));
        return false;
    }
    say(QObject::tr("The Mothion Link add-on is installed and turned on. Start Blender again (or turn the add-on on in its "
                    "preferences if it is open)."));
    return true;
}

} // namespace mothion::io
