#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QKeySequenceEdit;
class QLineEdit;

namespace mothion::ui {

// The Settings window (File > Settings): sections on the left, their options on the right. Every option
// applies as soon as it is changed (there is no OK to press); the window only closes.
//
//  - General: Discord presence, theme, how many recent projects to keep;
//  - Project: the size, frame rate, length and paper of a new project;
//  - Canvas: the zoom and rotation steps, the brush outline (and the pointer it replaces);
//  - Input: the pressure of a mouse;
//  - Advanced: the undo history, the data folder;
//  - Shortcuts: every command and its key, changed here (see Shortcuts), with the keys that are not changeable.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void recentListCleared();

private:
    QWidget *generalPage();
    QWidget *projectPage();
    QWidget *canvasPage();
    QWidget *inputPage();
    QWidget *advancedPage();
    QWidget *shortcutsPage();
    void fillShortcuts();
    void refreshShortcutRow(QTreeWidgetItem *item);
    QString selectedCommand() const;
    void assignKey();

    QListWidget *m_sections;
    QStackedWidget *m_pages;
    QTreeWidget *m_commands = nullptr;
    QKeySequenceEdit *m_keyEdit = nullptr;
    QLineEdit *m_filter = nullptr;
};

} // namespace mothion::ui
