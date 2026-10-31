#include "ui/panels/LayersPanel.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace mothion::ui {

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *list = new QListWidget(this);
    for (const QString &name : {tr("Effetti"), tr("Personaggio – linee"), tr("Personaggio – colore"),
                                tr("Sfondo")}) {
        auto *item = new QListWidgetItem(name, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    list->setCurrentRow(1);

    auto *note = new QLabel(tr("Segnaposto – nessun modello dati"), this);
    note->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(list, 1);
    layout->addWidget(note);
}

} // namespace mothion::ui
