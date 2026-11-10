#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <QTemporaryDir>

#include <list>
#include <optional>

namespace mothion::io {

// The frames got from Blender, kept so they need not be asked for again: by scene (the key of a BlenderSession) and frame.
// It is also the buffer of an update: the frames of a scene that has changed come in here, each as it arrives, and are put
// in the document when they are all there (or the one being looked at at once).
//
// Each scene has a generation. A change in the scene makes a new one (invalidate()): what is in the cache stays, to be
// shown and to be replaced, but is no longer fresh, and fresh frames are those of the generation now.
//
// The frames are kept in memory up to a limit; those used least are put on disk (a folder of the cache's own, taken away
// with it) and read from there when wanted.
class BlenderFrameCache
{
public:
    explicit BlenderFrameCache(qint64 memoryLimit = qint64(512) << 20);

    void setMemoryLimit(qint64 bytes);
    qint64 memoryBytes() const { return m_memory; }

    // A frame of the generation now (it replaces what was there).
    void put(const QString &key, int frame, const QImage &image);
    // The frame, whatever generation it is of (a null image if there is none).
    QImage get(const QString &key, int frame);
    bool has(const QString &key, int frame) const;
    // The frame is there and of the generation now.
    bool isFresh(const QString &key, int frame) const;

    quint64 generation(const QString &key) const { return m_generation.value(key, 1); }
    // The scene changed: what is kept is old now. Returns the new generation.
    quint64 invalidate(const QString &key);
    // The frames kept (fresh or not), in order, and how many of them are fresh.
    QList<int> frames(const QString &key) const;
    int freshCount(const QString &key) const;
    // Forgets all the frames of the scene (memory and disk).
    void clear(const QString &key);

private:
    struct Entry
    {
        QImage image;       // null when it is on disk
        QString file;       // where it was put on disk (empty: not there)
        quint64 generation = 0;
        std::list<std::pair<QString, int>>::iterator used; // its place in the order of use
        qint64 bytes = 0;
    };
    using Id = std::pair<QString, int>;
    static qint64 sizeOf(const QImage &image) { return qint64(image.sizeInBytes()); }
    void touch(Entry &entry, const Id &id);
    void trim();
    bool spill(const Id &id, Entry &entry);
    QString fileFor(const Id &id);

    QHash<QString, QHash<int, Entry>> m_entries; // by scene, by frame
    QHash<QString, quint64> m_generation;
    std::list<Id> m_order; // from the one used most lately to the one used least
    qint64 m_memory = 0;
    qint64 m_limit;
    std::optional<QTemporaryDir> m_disk;
    int m_files = 0;
};

} // namespace mothion::io
