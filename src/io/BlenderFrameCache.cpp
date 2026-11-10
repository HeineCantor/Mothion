#include "io/BlenderFrameCache.h"

#include <QDir>
#include <QFile>

#include <algorithm>

namespace mothion::io {

BlenderFrameCache::BlenderFrameCache(qint64 memoryLimit)
    : m_limit(memoryLimit)
{
}

void BlenderFrameCache::setMemoryLimit(qint64 bytes)
{
    m_limit = std::max<qint64>(0, bytes);
    trim();
}

void BlenderFrameCache::touch(Entry &entry, const Id &id)
{
    m_order.erase(entry.used);
    m_order.push_front(id);
    entry.used = m_order.begin();
}

QString BlenderFrameCache::fileFor(const Id &)
{
    if (!m_disk)
        m_disk.emplace();
    return m_disk->isValid() ? QDir(m_disk->path()).filePath(QStringLiteral("f%1.rgba").arg(++m_files)) : QString();
}

bool BlenderFrameCache::spill(const Id &id, Entry &entry)
{
    if (entry.image.isNull())
        return true;
    if (entry.file.isEmpty())
        entry.file = fileFor(id);
    if (entry.file.isEmpty())
        return false;
    QFile file(entry.file);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QImage image = entry.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qint32 size[2] = {image.width(), image.height()};
    file.write(reinterpret_cast<const char *>(size), sizeof(size));
    for (int y = 0; y < image.height(); ++y)
        file.write(reinterpret_cast<const char *>(image.constScanLine(y)), qint64(image.width()) * 4);
    return true;
}

void BlenderFrameCache::trim()
{
    // The ones used least go to disk (and leave memory) until it fits.
    for (auto it = m_order.end(); m_memory > m_limit && it != m_order.begin();) {
        --it;
        auto scene = m_entries.find(it->first);
        if (scene == m_entries.end())
            continue;
        auto entry = scene->find(it->second);
        if (entry == scene->end() || entry->image.isNull())
            continue;
        if (!spill(*it, *entry))
            continue; // no disk: it stays
        m_memory -= entry->bytes;
        entry->image = QImage();
    }
}

void BlenderFrameCache::put(const QString &key, int frame, const QImage &image)
{
    if (image.isNull())
        return;
    const Id id{key, frame};
    QHash<int, Entry> &scene = m_entries[key];
    auto it = scene.find(frame);
    if (it == scene.end()) {
        m_order.push_front(id);
        Entry entry;
        entry.used = m_order.begin();
        it = scene.insert(frame, entry);
    } else {
        if (!it->image.isNull())
            m_memory -= it->bytes;
        touch(*it, id);
    }
    it->image = image;
    it->bytes = sizeOf(image);
    it->generation = generation(key);
    if (!it->file.isEmpty()) { // (what was on disk is the old picture)
        QFile::remove(it->file);
        it->file.clear();
    }
    m_memory += it->bytes;
    trim();
}

QImage BlenderFrameCache::get(const QString &key, int frame)
{
    auto scene = m_entries.find(key);
    if (scene == m_entries.end())
        return {};
    auto it = scene->find(frame);
    if (it == scene->end())
        return {};
    const Id id{key, frame};
    if (it->image.isNull()) { // on disk: read back
        QFile file(it->file);
        qint32 size[2] = {0, 0};
        if (!file.open(QIODevice::ReadOnly) || file.read(reinterpret_cast<char *>(size), sizeof(size)) != sizeof(size)
            || size[0] <= 0 || size[1] <= 0)
            return {};
        QImage image(size[0], size[1], QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < size[1]; ++y)
            if (file.read(reinterpret_cast<char *>(image.scanLine(y)), qint64(size[0]) * 4) != qint64(size[0]) * 4)
                return {};
        it->image = image;
        m_memory += it->bytes;
        touch(*it, id);
        const QImage result = it->image;
        trim();
        return result;
    }
    touch(*it, id);
    return it->image;
}

bool BlenderFrameCache::has(const QString &key, int frame) const
{
    return m_entries.value(key).contains(frame);
}

bool BlenderFrameCache::isFresh(const QString &key, int frame) const
{
    const auto scene = m_entries.constFind(key);
    if (scene == m_entries.cend())
        return false;
    const auto it = scene->constFind(frame);
    return it != scene->cend() && it->generation == generation(key);
}

quint64 BlenderFrameCache::invalidate(const QString &key)
{
    return m_generation[key] = generation(key) + 1;
}

QList<int> BlenderFrameCache::frames(const QString &key) const
{
    QList<int> frames = m_entries.value(key).keys();
    std::sort(frames.begin(), frames.end());
    return frames;
}

int BlenderFrameCache::freshCount(const QString &key) const
{
    int n = 0;
    for (auto it = m_entries.value(key).cbegin(); it != m_entries.value(key).cend(); ++it)
        n += it->generation == generation(key) ? 1 : 0;
    return n;
}

void BlenderFrameCache::clear(const QString &key)
{
    auto scene = m_entries.find(key);
    if (scene == m_entries.end())
        return;
    for (auto it = scene->begin(); it != scene->end(); ++it) {
        m_order.erase(it->used);
        if (!it->image.isNull())
            m_memory -= it->bytes;
        if (!it->file.isEmpty())
            QFile::remove(it->file);
    }
    m_entries.erase(scene);
}

} // namespace mothion::io
