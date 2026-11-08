#pragma once

#include "io/Autosave.h"

#include <QDialog>

class QTreeWidget;

namespace mothion::ui {

// File > Open backup: the copies Mothion kept of the projects (the last few of each, for a week), the newest first:
// the project, when the copy was made, and whether it holds work the project's file may not have. One is chosen to be
// opened as a new, unsaved project, which never goes over the original.
class BackupDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BackupDialog(const QList<io::autosave::Entry> &entries, QWidget *parent = nullptr);

    // The copy chosen (valid after Accepted).
    io::autosave::Entry selected() const;

private:
    QTreeWidget *m_list;
    QList<io::autosave::Entry> m_entries;
};

} // namespace mothion::ui
