#include "ui/panels/LayersPanel.h"

#include "ui/IconFactory.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

// Thumbnail: checkerboard for a transparent layer, plain white for the paper.
QPixmap thumbnail(bool paper)
{
    QPixmap pm(36, 26);
    QPainter p(&pm);
    if (paper) {
        pm.fill(Qt::white);
    } else {
        for (int y = 0; y < pm.height(); y += 6)
            for (int x = 0; x < pm.width(); x += 6)
                p.fillRect(x, y, 6, 6, ((x + y) / 6) % 2 ? QColor(0xcc, 0xcc, 0xcc) : Qt::white);
    }
    p.setPen(QColor(0x18, 0x18, 0x1a));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return pm;
}

QWidget *layerRow(const QString &name, const QString &info, bool paper, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *eye = new QToolButton(row);
    eye->setObjectName(QStringLiteral("LayerEye")); // styled without the "checked" fill
    eye->setIconSize(QSize(14, 14));
    eye->setCheckable(true);
    eye->setAutoRaise(true);
    // Visible: eye icon; hidden: empty slot.
    QObject::connect(eye, &QToolButton::toggled, eye,
                     [eye](bool on) { eye->setIcon(on ? icon(IconId::Eye) : QIcon()); });
    eye->setChecked(true);
    eye->setToolTip(QObject::tr("Mostra / nascondi"));
    auto *thumb = new QLabel(row);
    thumb->setPixmap(thumbnail(paper));
    auto *text = new QLabel(info.isEmpty() ? name : QStringLiteral("%1\n%2").arg(info, name), row);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(6);
    layout->addWidget(eye);
    layout->addWidget(thumb);
    layout->addWidget(text, 1);
    return row;
}

} // namespace

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *blend = new QComboBox(this);
    blend->addItems({tr("Normale"), tr("Moltiplica"), tr("Scherma"), tr("Sovrapponi"), tr("Aggiungi")});
    auto *opacity = new QSlider(Qt::Horizontal, this);
    opacity->setRange(0, 100);
    opacity->setValue(100);
    auto *opacityValue = new QSpinBox(this);
    opacityValue->setRange(0, 100);
    opacityValue->setValue(100);
    opacityValue->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(opacity, &QSlider::valueChanged, opacityValue, &QSpinBox::setValue);
    connect(opacityValue, &QSpinBox::valueChanged, opacity, &QSlider::setValue);

    auto *header = new QHBoxLayout;
    header->setSpacing(4);
    header->addWidget(blend);
    header->addWidget(opacity, 1);
    header->addWidget(opacityValue);

    auto *list = new QListWidget(this);
    list->setFrameShape(QFrame::NoFrame);
    struct Layer { QString name; QString info; bool paper; };
    const Layer layers[] = {
        {tr("Livello 1"), tr("100 % Normale"), false},
        {tr("Carta"), QString(), true},
    };
    for (const Layer &l : layers) {
        auto *item = new QListWidgetItem(list);
        QWidget *row = layerRow(l.name, l.info, l.paper, list);
        item->setSizeHint(row->sizeHint());
        list->setItemWidget(item, row);
    }
    list->setCurrentRow(0);

    auto *note = new QLabel(tr("Segnaposto – nessun modello dati"), this);
    note->setEnabled(false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(header);
    layout->addWidget(list, 1);
    layout->addWidget(note);
}

} // namespace mothion::ui
