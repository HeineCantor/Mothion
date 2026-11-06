#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>

class QAction;
class QKeyEvent;
class QShortcut;

namespace mothion::ui {

// One command that has keys: what the Settings window lists and the places that run it bind to.
struct ShortcutCommand
{
    QString id;       // "file.save", "tool.brush"...: what code asks for, and what is saved
    QString category; // the group it is listed under
    QString text;
    QList<QKeySequence> defaults;
};

// The keys of every command, in one place. The defaults are listed here (and nowhere else); a person's own
// choices are kept on top of them (QSettings, "shortcuts/<id>") and win. The places that run a command
// (menu actions, the tools' keys, a panel's own) ask for it by id with bind(), and follow the changes.
//
// Not here: keys that are held while something is done (Space and Tab to pan, Shift for a straight line,
// Alt for the eyedropper) and the keys a tool takes while it is open (Enter, Esc, Delete, the arrows).
class Shortcuts : public QObject
{
    Q_OBJECT

public:
    static Shortcuts &instance();

    const QList<ShortcutCommand> &commands() const { return m_commands; }
    const ShortcutCommand *command(const QString &id) const;

    // The keys now: the person's, or the defaults.
    QList<QKeySequence> keys(const QString &id) const;
    // The first of them (empty when the command has none).
    QKeySequence key(const QString &id) const;
    bool isCustom(const QString &id) const { return m_custom.contains(id); }

    // Sets (an empty list: no keys at all) or goes back to the defaults. Saved at once.
    void setKeys(const QString &id, const QList<QKeySequence> &keys);
    void reset(const QString &id);
    void resetAll();

    // The command that has `key` now, other than `exceptId`; empty when it is free.
    QString conflictFor(const QKeySequence &key, const QString &exceptId) const;
    // Whether `event` is one of the command's keys (the keypad does not count as a modifier).
    bool matches(const QString &id, const QKeyEvent *event) const;

    // The action (or shortcut) gets the command's keys now and every time they change.
    void bind(QAction *action, const QString &id, Qt::ShortcutContext context = Qt::WindowShortcut);
    void bind(QShortcut *shortcut, const QString &id);

    // The tools' ids ("tool.brush"...), as the palette and its tooltips ask for them.
    static QString toolCommand(int tool);

signals:
    void changed(const QString &id);

private:
    Shortcuts();
    void add(const QString &id, const QString &category, const QString &text, const QList<QKeySequence> &defaults);
    void load();

    QList<ShortcutCommand> m_commands;
    QHash<QString, int> m_index;
    QHash<QString, QList<QKeySequence>> m_custom;
};

} // namespace mothion::ui
