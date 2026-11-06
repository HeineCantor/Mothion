#include "tools/ColorSetLibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace mothion::tools {

namespace {

// The built-in set: greys, then rows of hues at different saturation and value, so skin, shadow and pastel tones
// are all one click away.
ColorSet builtInSet()
{
    constexpr int kColumns = 8;
    ColorSet set;
    set.id = ColorSetLibrary::kDefaultId;
    set.name = QObject::tr("Default");
    set.builtIn = true;
    for (int i = 0; i < kColumns; ++i)
        set.swatches.append({QColor::fromHsvF(0, 0, float(i) / (kColumns - 1)), {}});
    const float rows[][2] = {{0.9f, 0.9f}, {0.55f, 1.0f}, {0.25f, 1.0f}, {0.8f, 0.55f}, {0.45f, 0.75f}};
    for (const auto &sv : rows)
        for (int i = 0; i < kColumns; ++i)
            set.swatches.append({QColor::fromHsvF(float(i) / kColumns, sv[0], sv[1]), {}});
    return set;
}

QByteArray toJson(const ColorSet &set)
{
    QJsonArray colors;
    for (const ColorSwatch &s : set.swatches) {
        QJsonObject o{{"c", s.color.name(QColor::HexArgb)}};
        if (!s.name.isEmpty())
            o["n"] = s.name;
        colors.append(o);
    }
    return QJsonDocument(QJsonObject{{"name", set.name}, {"colors", colors}}).toJson(QJsonDocument::Indented);
}

} // namespace

ColorSetLibrary::ColorSetLibrary(const QString &folder, QObject *parent)
    : QObject(parent)
    , m_folder(folder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                      + QStringLiteral("/colorsets")
                                : folder)
    , m_default(builtInSet())
{
    load();
}

void ColorSetLibrary::load()
{
    const QDir dir(m_folder);
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile in(dir.filePath(file));
        if (!in.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
        if (root.isEmpty() || !root.contains("colors"))
            continue; // not one of ours
        ColorSet set;
        set.id = QFileInfo(file).completeBaseName();
        set.name = root.value("name").toString(set.id);
        for (const QJsonValue &v : root.value("colors").toArray()) {
            const QJsonObject o = v.toObject();
            const QColor color(o.value("c").toString());
            if (color.isValid())
                set.swatches.append({color, o.value("n").toString()});
        }
        m_sets.append(std::move(set));
    }
    std::sort(m_sets.begin(), m_sets.end(),
              [](const ColorSet &a, const ColorSet &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    QFile active(dir.filePath(QStringLiteral("active.txt")));
    if (active.open(QIODevice::ReadOnly)) {
        const QString id = QString::fromUtf8(active.readAll()).trimmed();
        if (set(id))
            m_active = id;
    }
}

QList<ColorSet> ColorSetLibrary::sets() const
{
    QList<ColorSet> all{m_default};
    all += m_sets;
    return all;
}

const ColorSet *ColorSetLibrary::set(const QString &id) const
{
    if (id == m_default.id)
        return &m_default;
    for (const ColorSet &s : m_sets)
        if (s.id == id)
            return &s;
    return nullptr;
}

ColorSet *ColorSetLibrary::editable(const QString &id)
{
    for (ColorSet &s : m_sets)
        if (s.id == id)
            return &s;
    return nullptr; // unknown, or the built-in one
}

bool ColorSetLibrary::save(const ColorSet &set)
{
    if (!QDir().mkpath(m_folder))
        return false;
    QSaveFile out(QDir(m_folder).filePath(set.id + QStringLiteral(".json")));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(toJson(set));
    return out.commit();
}

void ColorSetLibrary::saveActive() const
{
    if (!QDir().mkpath(m_folder))
        return;
    QSaveFile out(QDir(m_folder).filePath(QStringLiteral("active.txt")));
    if (out.open(QIODevice::WriteOnly)) {
        out.write(m_active.toUtf8());
        out.commit();
    }
}

void ColorSetLibrary::setActive(const QString &id)
{
    if (!set(id) || id == m_active)
        return;
    m_active = id;
    saveActive();
    emit activeChanged();
}

QString ColorSetLibrary::uniqueName(const QString &wanted) const
{
    const QString base = wanted.trimmed().isEmpty() ? tr("New set") : wanted.trimmed();
    auto taken = [this](const QString &name) {
        for (const ColorSet &s : sets())
            if (s.name.compare(name, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    };
    QString name = base;
    for (int n = 2; taken(name); ++n)
        name = QStringLiteral("%1 %2").arg(base).arg(n);
    return name;
}

QString ColorSetLibrary::create(const QString &name)
{
    ColorSet set;
    set.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    set.name = uniqueName(name);
    if (!save(set))
        return {};
    m_sets.append(set);
    std::sort(m_sets.begin(), m_sets.end(),
              [](const ColorSet &a, const ColorSet &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    emit setsChanged();
    return set.id;
}

QString ColorSetLibrary::duplicate(const QString &id, const QString &name)
{
    const ColorSet *from = set(id);
    if (!from)
        return {};
    ColorSet copy = *from;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = uniqueName(name.isEmpty() ? tr("%1 copy").arg(from->name) : name);
    copy.builtIn = false;
    if (!save(copy))
        return {};
    m_sets.append(copy);
    std::sort(m_sets.begin(), m_sets.end(),
              [](const ColorSet &a, const ColorSet &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    emit setsChanged();
    return copy.id;
}

bool ColorSetLibrary::rename(const QString &id, const QString &name)
{
    ColorSet *s = editable(id);
    if (!s || name.trimmed().isEmpty())
        return false;
    ColorSet changed = *s;
    changed.name = name.trimmed();
    if (changed.name.compare(s->name, Qt::CaseInsensitive) != 0)
        changed.name = uniqueName(changed.name);
    if (!save(changed))
        return false;
    *s = changed;
    std::sort(m_sets.begin(), m_sets.end(),
              [](const ColorSet &a, const ColorSet &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    emit setsChanged();
    return true;
}

bool ColorSetLibrary::remove(const QString &id)
{
    if (!editable(id))
        return false;
    QFile::remove(QDir(m_folder).filePath(id + QStringLiteral(".json")));
    m_sets.removeIf([&id](const ColorSet &s) { return s.id == id; });
    const bool wasActive = m_active == id;
    if (wasActive) {
        m_active = kDefaultId;
        saveActive();
    }
    emit setsChanged();
    if (wasActive)
        emit activeChanged();
    return true;
}

bool ColorSetLibrary::addColor(const QString &id, const QColor &color, const QString &name)
{
    ColorSet *s = editable(id);
    if (!s || !color.isValid())
        return false;
    s->swatches.append({color, name});
    if (!save(*s))
        return false;
    emit setChanged(id);
    return true;
}

bool ColorSetLibrary::setColor(const QString &id, int index, const QColor &color)
{
    ColorSet *s = editable(id);
    if (!s || index < 0 || index >= s->swatches.size() || !color.isValid())
        return false;
    s->swatches[index].color = color;
    if (!save(*s))
        return false;
    emit setChanged(id);
    return true;
}

bool ColorSetLibrary::setColorName(const QString &id, int index, const QString &name)
{
    ColorSet *s = editable(id);
    if (!s || index < 0 || index >= s->swatches.size())
        return false;
    s->swatches[index].name = name.trimmed();
    if (!save(*s))
        return false;
    emit setChanged(id);
    return true;
}

bool ColorSetLibrary::removeColor(const QString &id, int index)
{
    ColorSet *s = editable(id);
    if (!s || index < 0 || index >= s->swatches.size())
        return false;
    s->swatches.removeAt(index);
    if (!save(*s))
        return false;
    emit setChanged(id);
    return true;
}

bool ColorSetLibrary::moveColor(const QString &id, int from, int to)
{
    ColorSet *s = editable(id);
    if (!s || from < 0 || from >= s->swatches.size() || to < 0 || to >= s->swatches.size())
        return false;
    if (from == to)
        return true;
    s->swatches.move(from, to);
    if (!save(*s))
        return false;
    emit setChanged(id);
    return true;
}

} // namespace mothion::tools
