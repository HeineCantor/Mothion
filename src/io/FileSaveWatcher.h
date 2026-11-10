#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

namespace mothion::io {

// Tells when a file is saved by another program (Blender saving a scene, say) while this one has it linked. Programs
// save in different ways: some write over the file, others write a copy and rename it over the old one (the file under
// the watch is then a new one), so the folder is watched too, and a change is only spoken of once things have been
// quiet for a moment (`quietMs`) and the file is not what it was (its date or its size). A file that is not there (moved,
// being replaced) is not an event.
class FileSaveWatcher : public QObject
{
    Q_OBJECT

public:
    explicit FileSaveWatcher(QObject *parent = nullptr, int quietMs = 600);

    // The files to watch from now on (the ones already watched keep what is known of them).
    void setPaths(const QStringList &paths);
    QStringList paths() const { return m_known.keys(); }

signals:
    // The file was saved.
    void saved(const QString &path);

private:
    struct Signature
    {
        QDateTime modified;
        qint64 size = -1;
        bool operator==(const Signature &) const = default;
    };
    static Signature signatureOf(const QString &path);
    void rewatch();
    void check();

    QFileSystemWatcher m_watcher;
    QHash<QString, Signature> m_known;
    QTimer m_quiet;
};

} // namespace mothion::io
