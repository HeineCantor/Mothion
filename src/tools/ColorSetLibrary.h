#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

namespace mothion::tools {

struct ColorSwatch
{
    QColor color;
    QString name; // may be empty: the hex code is shown then

    bool operator==(const ColorSwatch &) const = default;
};

struct ColorSet
{
    QString id;
    QString name;
    bool builtIn = false; // cannot be changed, renamed or deleted: duplicate it to change it
    QList<ColorSwatch> swatches;
};

// The user's color sets, kept in the application data folder (colorsets/, a JSON file for each set), so they are
// there in every project and workspace. The built-in Default set comes first and cannot be edited. The active
// set (the one the Color set panel shows) is remembered between runs. Every change is written at once and
// announced with a signal, so views follow it.
class ColorSetLibrary : public QObject
{
    Q_OBJECT

public:
    static inline const QString kDefaultId = QStringLiteral("default");

    // `folder`: where the sets live (default: the application data folder's colorsets/).
    explicit ColorSetLibrary(const QString &folder = {}, QObject *parent = nullptr);

    QString storageFolder() const { return m_folder; }

    // The built-in set first, then the others by name.
    QList<ColorSet> sets() const;
    const ColorSet *set(const QString &id) const;

    QString activeId() const { return m_active; }
    void setActive(const QString &id);

    // These return the new set's id (empty if nothing was made) or whether the change was made; the built-in set
    // refuses every change but being duplicated.
    QString create(const QString &name);
    QString duplicate(const QString &id, const QString &name = {});
    bool rename(const QString &id, const QString &name);
    bool remove(const QString &id);

    bool addColor(const QString &id, const QColor &color, const QString &name = {});
    bool setColor(const QString &id, int index, const QColor &color); // keeps the name
    bool setColorName(const QString &id, int index, const QString &name);
    bool removeColor(const QString &id, int index);
    bool moveColor(const QString &id, int from, int to); // `to`: the index the color has afterwards

signals:
    void setsChanged();                // a set was added, removed or renamed
    void setChanged(const QString &id); // the colors of a set changed
    void activeChanged();

private:
    ColorSet *editable(const QString &id);
    bool save(const ColorSet &set);
    void saveActive() const;
    void load();
    QString uniqueName(const QString &wanted) const;

    QString m_folder;
    QList<ColorSet> m_sets; // not the built-in one
    ColorSet m_default;
    QString m_active = kDefaultId;
};

} // namespace mothion::tools
