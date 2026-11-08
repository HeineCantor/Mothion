#include "ui/BackupDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace mothion::ui {

BackupDialog::BackupDialog(const QList<io::autosave::Entry> &entries, QWidget *parent)
    : QDialog(parent)
    , m_list(new QTreeWidget(this))
    , m_entries(entries)
{
    setObjectName(QStringLiteral("BackupDialog"));
    setWindowTitle(tr("Open backup"));
    resize(560, 360);

    auto *note = new QLabel(tr("Copies Mothion made while you worked, the last %1 of each project, kept for %2 days. "
                               "A copy opens as a new project: it is never saved over the original.")
                                .arg(io::autosave::kCopiesKept)
                                .arg(io::autosave::kDaysKept),
                            this);
    note->setWordWrap(true);
    m_list->setObjectName(QStringLiteral("backups"));
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({tr("Project"), tr("Made"), tr("")});
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 0; i < m_entries.size(); ++i) {
        const io::autosave::Entry &entry = m_entries[i];
        auto *item = new QTreeWidgetItem(m_list);
        item->setText(0, entry.name);
        item->setToolTip(0, entry.projectPath.isEmpty() ? tr("A project that was not saved yet") : entry.projectPath);
        item->setText(1, QLocale().toString(entry.when, QLocale::ShortFormat));
        item->setText(2, entry.retired ? tr("the project was saved or closed after it") : tr("work not saved"));
        item->setData(0, Qt::UserRole, i);
    }
    if (m_list->topLevelItemCount() > 0)
        m_list->setCurrentItem(m_list->topLevelItem(0));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Open)->setEnabled(m_list->topLevelItemCount() > 0);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &QDialog::accept);
    auto *empty = new QLabel(tr("There are no copies yet."), this);
    empty->setVisible(m_list->topLevelItemCount() == 0);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addWidget(m_list, 1);
    layout->addWidget(empty);
    layout->addWidget(buttons);
}

io::autosave::Entry BackupDialog::selected() const
{
    const QTreeWidgetItem *item = m_list->currentItem();
    return item ? m_entries.value(item->data(0, Qt::UserRole).toInt()) : io::autosave::Entry{};
}

} // namespace mothion::ui
