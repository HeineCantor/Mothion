#include "ui/Shortcuts.h"

#include "tools/ToolState.h"

#include <QAction>
#include <QKeyEvent>
#include <QSettings>
#include <QShortcut>

#include <algorithm>
#include <iterator>

namespace mothion::ui {

namespace {

constexpr auto kGroup = "shortcuts";
// A saved list starts with this, so an empty one (no keys) is told from none saved.
constexpr auto kMarker = "v1";

QKeySequence seq(Qt::Key k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    return QKeySequence(QKeyCombination(mods, k));
}

QKeySequence ctrl(Qt::Key k, Qt::KeyboardModifiers more = Qt::NoModifier)
{
    return seq(k, Qt::ControlModifier | more);
}

// The platform's keys for a standard command, without the dedicated keys some keyboards have for it
// (Key_New, Key_Paste...): they are not keys to type, and would only clutter the list of shortcuts.
QList<QKeySequence> standard(QKeySequence::StandardKey k)
{
    static const Qt::Key dedicated[] = {Qt::Key_New, Qt::Key_Open, Qt::Key_Save, Qt::Key_Undo, Qt::Key_Redo,
                                        Qt::Key_Cut, Qt::Key_Copy, Qt::Key_Paste, Qt::Key_Exit};
    QList<QKeySequence> keys = QKeySequence::keyBindings(k);
    keys.removeIf([](const QKeySequence &key) {
        return !key.isEmpty() && std::find(std::begin(dedicated), std::end(dedicated), key[0].key()) != std::end(dedicated);
    });
    return keys;
}

} // namespace

Shortcuts &Shortcuts::instance()
{
    static Shortcuts shortcuts;
    return shortcuts;
}

Shortcuts::Shortcuts()
{
    const QString file = tr("File"), edit = tr("Edit"), view = tr("View"), animation = tr("Animation"),
                  tools = tr("Tools"), drawing = tr("Drawing"), other = tr("Panels");
    add(QStringLiteral("file.new"), file, tr("New"), standard(QKeySequence::New));
    add(QStringLiteral("file.open"), file, tr("Open…"), standard(QKeySequence::Open));
    add(QStringLiteral("file.save"), file, tr("Save"), standard(QKeySequence::Save));
    add(QStringLiteral("file.saveAs"), file, tr("Save as…"), {ctrl(Qt::Key_S, Qt::ShiftModifier)});
    add(QStringLiteral("file.export"), file, tr("Export frames…"), {ctrl(Qt::Key_E)});
    // Some platforms have only a dedicated Exit key for it: then Ctrl+Q.
    QList<QKeySequence> quitKeys = standard(QKeySequence::Quit);
    if (quitKeys.isEmpty())
        quitKeys << ctrl(Qt::Key_Q);
    add(QStringLiteral("file.quit"), file, tr("Quit"), quitKeys);

    // Undo and redo: the platform's keys, and for redo both Ctrl+Shift+Z and Ctrl+Y everywhere.
    QList<QKeySequence> redoKeys = standard(QKeySequence::Redo);
    for (const QKeySequence &k : {ctrl(Qt::Key_Z, Qt::ShiftModifier), ctrl(Qt::Key_Y)})
        if (!redoKeys.contains(k))
            redoKeys << k;
    add(QStringLiteral("edit.undo"), edit, tr("Undo"), standard(QKeySequence::Undo));
    add(QStringLiteral("edit.redo"), edit, tr("Redo"), redoKeys);
    add(QStringLiteral("edit.cut"), edit, tr("Cut"), standard(QKeySequence::Cut));
    add(QStringLiteral("edit.copy"), edit, tr("Copy"), standard(QKeySequence::Copy));
    add(QStringLiteral("edit.paste"), edit, tr("Paste"), standard(QKeySequence::Paste));
    add(QStringLiteral("edit.selectAll"), edit, tr("Select all"), standard(QKeySequence::SelectAll));
    add(QStringLiteral("edit.deselect"), edit, tr("Deselect"), {ctrl(Qt::Key_D)});
    add(QStringLiteral("edit.invertSelection"), edit, tr("Invert selection"), {ctrl(Qt::Key_I, Qt::ShiftModifier)});
    add(QStringLiteral("edit.transform"), edit, tr("Transform"), {ctrl(Qt::Key_T)});

    add(QStringLiteral("view.zoomOut"), view, tr("Zoom out"), {ctrl(Qt::Key_Minus)});
    add(QStringLiteral("view.zoomIn"), view, tr("Zoom in"), {ctrl(Qt::Key_Plus), ctrl(Qt::Key_Equal)});
    add(QStringLiteral("view.fit"), view, tr("Fit to window"), {ctrl(Qt::Key_0)});
    add(QStringLiteral("view.actualSize"), view, tr("Actual size"), {ctrl(Qt::Key_1)});
    add(QStringLiteral("view.rotateLeft"), view, tr("Rotate left"), {ctrl(Qt::Key_Comma)});
    add(QStringLiteral("view.rotateRight"), view, tr("Rotate right"), {ctrl(Qt::Key_Period)});
    add(QStringLiteral("view.rotateReset"), view, tr("Reset rotation"), {ctrl(Qt::Key_Slash)});
    add(QStringLiteral("view.debugOverlay"), view, tr("Input debug overlay"), {seq(Qt::Key_F12)});

    add(QStringLiteral("anim.playPause"), animation, tr("Play / pause the timeline"), {});
    add(QStringLiteral("anim.previousSlot"), animation, tr("Previous time slot"), {seq(Qt::Key_Comma)});
    add(QStringLiteral("anim.nextSlot"), animation, tr("Next time slot"), {seq(Qt::Key_Period)});
    add(QStringLiteral("anim.previousFrame"), animation, tr("Previous frame"), {seq(Qt::Key_Comma, Qt::ShiftModifier)});
    add(QStringLiteral("anim.nextFrame"), animation, tr("Next frame"), {seq(Qt::Key_Period, Qt::ShiftModifier)});
    add(QStringLiteral("anim.lengthen"), animation, tr("Lengthen frame"),
        {seq(Qt::Key_Plus), seq(Qt::Key_Plus, Qt::KeypadModifier)});
    add(QStringLiteral("anim.shorten"), animation, tr("Shorten frame"),
        {seq(Qt::Key_Minus), seq(Qt::Key_Minus, Qt::KeypadModifier)});

    using tools::ToolId;
    const struct { ToolId tool; Qt::Key k; } toolKeys[] = {
        {ToolId::Brush, Qt::Key_B},      {ToolId::Eraser, Qt::Key_E},     {ToolId::Eyedropper, Qt::Key_I},
        {ToolId::Blend, Qt::Key_U},      {ToolId::Liquify, Qt::Key_Q},    {ToolId::Shape, Qt::Key_S},
        {ToolId::RectSelect, Qt::Key_M}, {ToolId::LassoSelect, Qt::Key_L}, {ToolId::Fill, Qt::Key_G},
        {ToolId::Hand, Qt::Key_H},       {ToolId::Zoom, Qt::Key_Z},       {ToolId::Move, Qt::Key_V},
        {ToolId::Puppet, Qt::Key_P},
    };
    for (const auto &t : toolKeys) {
        const QString name = t.tool == ToolId::RectSelect ? tr("Selection") : tools::ToolState::displayName(t.tool);
        add(toolCommand(int(t.tool)), tools, name, {seq(t.k)});
    }
    add(QStringLiteral("color.switch"), drawing, tr("Switch between the main and the secondary color"),
        {seq(Qt::Key_X)});
    add(QStringLiteral("brush.smaller"), drawing, tr("Smaller brush"), {seq(Qt::Key_BracketLeft)});
    add(QStringLiteral("brush.bigger"), drawing, tr("Bigger brush"), {seq(Qt::Key_BracketRight)});

    add(QStringLiteral("layers.delete"), other, tr("Delete the selected layers (Layers panel)"),
        standard(QKeySequence::Delete));
    add(QStringLiteral("timeline.pasteCopies"), other, tr("Paste frames as copies (Timeline)"),
        {ctrl(Qt::Key_V, Qt::ShiftModifier)});
    load();
}

void Shortcuts::add(const QString &id, const QString &category, const QString &text,
                    const QList<QKeySequence> &defaults)
{
    m_index.insert(id, int(m_commands.size()));
    m_commands.push_back({id, category, text, defaults});
}

void Shortcuts::load()
{
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    for (const ShortcutCommand &c : std::as_const(m_commands)) {
        const QStringList saved = settings.value(c.id).toStringList();
        if (saved.isEmpty() || saved.first() != QLatin1String(kMarker))
            continue;
        QList<QKeySequence> keys;
        for (int i = 1; i < saved.size(); ++i)
            keys << QKeySequence(saved[i], QKeySequence::PortableText);
        m_custom.insert(c.id, keys);
    }
}

const ShortcutCommand *Shortcuts::command(const QString &id) const
{
    const auto it = m_index.constFind(id);
    return it == m_index.constEnd() ? nullptr : &m_commands[it.value()];
}

QList<QKeySequence> Shortcuts::keys(const QString &id) const
{
    if (const auto it = m_custom.constFind(id); it != m_custom.constEnd())
        return it.value();
    const ShortcutCommand *c = command(id);
    return c ? c->defaults : QList<QKeySequence>();
}

QKeySequence Shortcuts::key(const QString &id) const
{
    const QList<QKeySequence> all = keys(id);
    return all.isEmpty() ? QKeySequence() : all.first();
}

void Shortcuts::setKeys(const QString &id, const QList<QKeySequence> &keys)
{
    const ShortcutCommand *c = command(id);
    if (!c)
        return;
    if (keys == c->defaults) {
        reset(id);
        return;
    }
    if (m_custom.value(id) == keys && m_custom.contains(id))
        return;
    m_custom.insert(id, keys);
    QStringList saved{QLatin1String(kMarker)};
    for (const QKeySequence &k : keys)
        saved << k.toString(QKeySequence::PortableText);
    QSettings settings;
    settings.setValue(QStringLiteral("%1/%2").arg(QLatin1String(kGroup), id), saved);
    emit changed(id);
}

void Shortcuts::reset(const QString &id)
{
    if (!m_custom.remove(id))
        return;
    QSettings settings;
    settings.remove(QStringLiteral("%1/%2").arg(QLatin1String(kGroup), id));
    emit changed(id);
}

void Shortcuts::resetAll()
{
    const QList<QString> ids = m_custom.keys();
    for (const QString &id : ids)
        reset(id);
}

void Shortcuts::assign(const QString &id, const QKeySequence &key)
{
    if (key.isEmpty())
        return;
    const QString other = conflictFor(key, id);
    if (!other.isEmpty()) {
        QList<QKeySequence> rest = keys(other);
        rest.removeAll(key);
        setKeys(other, rest);
    }
    setKeys(id, {key});
}

QString Shortcuts::conflictFor(const QKeySequence &key, const QString &exceptId) const
{
    if (key.isEmpty())
        return {};
    for (const ShortcutCommand &c : m_commands)
        if (c.id != exceptId && keys(c.id).contains(key))
            return c.id;
    return {};
}

bool Shortcuts::matches(const QString &id, const QKeyEvent *event) const
{
    const QKeySequence pressed(QKeyCombination(event->modifiers() & ~Qt::KeypadModifier, Qt::Key(event->key())));
    for (const QKeySequence &k : keys(id))
        if (k == pressed)
            return true;
    return false;
}

void Shortcuts::bind(QAction *action, const QString &id, Qt::ShortcutContext context)
{
    action->setShortcutContext(context);
    action->setShortcuts(keys(id));
    connect(this, &Shortcuts::changed, action, [this, action, id](const QString &which) {
        if (which == id)
            action->setShortcuts(keys(id));
    });
}

void Shortcuts::bind(QShortcut *shortcut, const QString &id)
{
    shortcut->setKeys(keys(id));
    connect(this, &Shortcuts::changed, shortcut, [this, shortcut, id](const QString &which) {
        if (which == id)
            shortcut->setKeys(keys(id));
    });
}

QString Shortcuts::toolCommand(int tool)
{
    static const QHash<int, QString> names = {
        {int(tools::ToolId::Brush), QStringLiteral("tool.brush")},
        {int(tools::ToolId::Eraser), QStringLiteral("tool.eraser")},
        {int(tools::ToolId::Eyedropper), QStringLiteral("tool.eyedropper")},
        {int(tools::ToolId::Blend), QStringLiteral("tool.blend")},
        {int(tools::ToolId::Liquify), QStringLiteral("tool.liquify")},
        {int(tools::ToolId::Shape), QStringLiteral("tool.shape")},
        {int(tools::ToolId::RectSelect), QStringLiteral("tool.select")},
        {int(tools::ToolId::LassoSelect), QStringLiteral("tool.lasso")},
        {int(tools::ToolId::Fill), QStringLiteral("tool.fill")},
        {int(tools::ToolId::Hand), QStringLiteral("tool.hand")},
        {int(tools::ToolId::Zoom), QStringLiteral("tool.zoom")},
        {int(tools::ToolId::Move), QStringLiteral("tool.move")},
        {int(tools::ToolId::Puppet), QStringLiteral("tool.puppet")},
    };
    return names.value(tool);
}

} // namespace mothion::ui
