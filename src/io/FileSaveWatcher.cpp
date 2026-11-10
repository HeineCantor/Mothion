#include "io/FileSaveWatcher.h"

#include <QFileInfo>

namespace mothion::io {

FileSaveWatcher::FileSaveWatcher(QObject *parent, int quietMs)
    : QObject(parent)
{
    m_quiet.setSingleShot(true);
    m_quiet.setInterval(quietMs);
    connect(&m_quiet, &QTimer::timeout, this, &FileSaveWatcher::check);
    auto changed = [this] {
        rewatch(); // a file that was replaced is a new one to the watcher: it is put back
        m_quiet.start(); // and the check waits until the saving is over
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, changed);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, changed);
}

FileSaveWatcher::Signature FileSaveWatcher::signatureOf(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() ? Signature{info.lastModified(), info.size()} : Signature{};
}

void FileSaveWatcher::setPaths(const QStringList &paths)
{
    QHash<QString, Signature> known;
    for (const QString &path : paths)
        known.insert(path, m_known.contains(path) ? m_known.value(path) : signatureOf(path));
    m_known = known;
    rewatch();
}

void FileSaveWatcher::rewatch()
{
    QStringList wanted;
    for (auto it = m_known.cbegin(); it != m_known.cend(); ++it) {
        wanted << it.key();
        wanted << QFileInfo(it.key()).absolutePath(); // the folder: the file may be replaced
    }
    wanted.removeDuplicates();
    const QStringList watching = m_watcher.files() + m_watcher.directories();
    if (!watching.isEmpty())
        m_watcher.removePaths(watching);
    for (const QString &path : wanted)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);
}

void FileSaveWatcher::check()
{
    QStringList savedFiles;
    for (auto it = m_known.begin(); it != m_known.end(); ++it) {
        const Signature now = signatureOf(it.key());
        if (now.size < 0 || now == it.value()) // gone for now, or as it was
            continue;
        it.value() = now;
        savedFiles << it.key();
    }
    rewatch();
    for (const QString &path : savedFiles) // (a listener may change the paths)
        emit saved(path);
}

} // namespace mothion::io
