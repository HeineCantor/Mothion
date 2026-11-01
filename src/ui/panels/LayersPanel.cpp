#include "ui/panels/LayersPanel.h"

#include "canvas/TileQt.h"
#include "core/document/commands/LayerCommands.h"
#include "document/DocumentController.h"
#include "ui/IconFactory.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mothion::ui {

using document::DocumentController;

namespace {

constexpr QSize kThumbSize(40, 24);
constexpr int kLayerIdRole = Qt::UserRole + 1;

const doc::BlendMode kBlendModes[] = {doc::BlendMode::Normal, doc::BlendMode::Multiply, doc::BlendMode::Screen,
                                      doc::BlendMode::Overlay, doc::BlendMode::Add};

QString blendName(doc::BlendMode mode)
{
    switch (mode) {
    case doc::BlendMode::Normal: return LayersPanel::tr("Normal");
    case doc::BlendMode::Multiply: return LayersPanel::tr("Multiply");
    case doc::BlendMode::Screen: return LayersPanel::tr("Screen");
    case doc::BlendMode::Overlay: return LayersPanel::tr("Overlay");
    case doc::BlendMode::Add: return LayersPanel::tr("Add");
    }
    return {};
}

// Thumbnail of the drawing shown at the current frame: checkerboard (or
// paper color) background, tiles scaled down without building a full image.
QPixmap thumbnail(const doc::Drawing *drawing, const QSize &docSize, const QColor *paper)
{
    QImage img(kThumbSize, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    if (paper) {
        img.fill(*paper);
    } else {
        for (int y = 0; y < img.height(); y += 6)
            for (int x = 0; x < img.width(); x += 6)
                p.fillRect(x, y, 6, 6, ((x + y) / 6) % 2 ? QColor(0xcc, 0xcc, 0xcc) : Qt::white);
    }
    if (drawing) {
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.scale(double(kThumbSize.width()) / docSize.width(), double(kThumbSize.height()) / docSize.height());
        for (const doc::TileCoord &c : drawing->image.tileCoords())
            p.drawImage(canvas::tileRect(c).topLeft(), canvas::tileImage(*drawing->image.tile(c)));
        p.restore();
    }
    p.setPen(QColor(0x18, 0x18, 0x1a));
    p.drawRect(img.rect().adjusted(0, 0, -1, -1));
    return QPixmap::fromImage(img);
}

QToolButton *toggleButton(IconId id, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("LayerEye")); // styled without the "checked" fill
    b->setIconSize(QSize(14, 14));
    b->setCheckable(true);
    b->setAutoRaise(true);
    b->setToolTip(tip);
    // Checked: icon shown; unchecked: empty slot.
    QObject::connect(b, &QToolButton::toggled, b, [b, id](bool on) { b->setIcon(on ? icon(id) : QIcon()); });
    return b;
}

QToolButton *actionButton(IconId id, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setIcon(icon(id));
    b->setIconSize(QSize(16, 16));
    b->setAutoRaise(true);
    b->setToolTip(tip);
    return b;
}

} // namespace

LayersPanel::LayersPanel(DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_blend(new QComboBox(this))
    , m_opacity(new QSlider(Qt::Horizontal, this))
    , m_opacityValue(new QSpinBox(this))
    , m_list(new QListWidget(this))
    , m_add(actionButton(IconId::Add, tr("New layer"), this))
    , m_remove(actionButton(IconId::Delete, tr("Delete layer"), this))
    , m_up(actionButton(IconId::ArrowUp, tr("Move up"), this))
    , m_down(actionButton(IconId::ArrowDown, tr("Move down"), this))
{
    for (doc::BlendMode mode : kBlendModes)
        m_blend->addItem(blendName(mode));
    m_opacity->setRange(0, 100);
    m_opacityValue->setRange(0, 100);
    m_opacityValue->setSuffix(QStringLiteral(" %"));
    m_opacityValue->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_opacityValue->setKeyboardTracking(false);
    m_list->setFrameShape(QFrame::NoFrame);

    auto *header = new QHBoxLayout;
    header->setSpacing(4);
    header->addWidget(m_blend);
    header->addWidget(m_opacity, 1);
    header->addWidget(m_opacityValue);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(0);
    buttons->addWidget(m_add);
    buttons->addWidget(m_remove);
    buttons->addStretch(1);
    buttons->addWidget(m_up);
    buttons->addWidget(m_down);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(header);
    layout->addWidget(m_list, 1);
    layout->addLayout(buttons);

    // Header edits.
    connect(m_blend, &QComboBox::activated, this, [this](int index) {
        editCurrentLayer([index](doc::LayerProperties &p) { p.blend = kBlendModes[index]; });
    });
    // Opacity: the slider previews in the spin box while dragged and is
    // committed once, on release, so a drag is a single undo step.
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        const QSignalBlocker block(m_opacityValue);
        m_opacityValue->setValue(v);
        if (!m_opacity->isSliderDown())
            commitOpacity();
    });
    connect(m_opacity, &QSlider::sliderReleased, this, &LayersPanel::commitOpacity);
    connect(m_opacityValue, &QSpinBox::valueChanged, this, [this](int v) {
        const QSignalBlocker block(m_opacity);
        m_opacity->setValue(v);
        commitOpacity();
    });

    // Stack edits.
    connect(m_add, &QToolButton::clicked, this, [this] {
        doc::Document &d = m_document->documentForCommands();
        const int index = d.indexOf(m_document->currentLayer()) + 1; // above the current one
        auto add = std::make_unique<doc::AddLayerCommand>(
            d, index, doc::LayerProperties{tr("Layer %1").arg(d.layerCount() + 1).toStdString()});
        const doc::LayerId id = add->layerId();
        m_document->execute(std::move(add));
        m_document->setCurrentLayer(id);
    });
    connect(m_remove, &QToolButton::clicked, this, [this] {
        m_document->execute(std::make_unique<doc::RemoveLayerCommand>(m_document->currentLayer()));
    });
    connect(m_up, &QToolButton::clicked, this, [this] {
        const doc::LayerId id = m_document->currentLayer();
        m_document->execute(std::make_unique<doc::MoveLayerCommand>(id, m_document->document().indexOf(id) + 1));
    });
    connect(m_down, &QToolButton::clicked, this, [this] {
        const doc::LayerId id = m_document->currentLayer();
        m_document->execute(std::make_unique<doc::MoveLayerCommand>(id, m_document->document().indexOf(id) - 1));
    });

    // Selection and renaming.
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && item->data(kLayerIdRole).isValid())
            m_document->setCurrentLayer(item->data(kLayerIdRole).value<doc::LayerId>());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item->data(kLayerIdRole).isValid())
            renameLayer(item->data(kLayerIdRole).value<doc::LayerId>());
    });

    // Model -> view.
    m_thumbnailTimer.setSingleShot(true);
    m_thumbnailTimer.setInterval(250);
    connect(&m_thumbnailTimer, &QTimer::timeout, this, &LayersPanel::refreshThumbnails);
    auto scheduleThumbnails = [this] {
        if (!m_thumbnailTimer.isActive())
            m_thumbnailTimer.start();
    };
    // Queued: a row's own button may trigger the change, and must not be
    // destroyed while its click is still being delivered.
    connect(m_document, &DocumentController::structureChanged, this, &LayersPanel::rebuildList,
            Qt::QueuedConnection);
    connect(m_document, &DocumentController::currentLayerChanged, this, [this] {
        selectCurrentRow();
        syncHeader();
    });
    connect(m_document, &DocumentController::pixelsChanged, this, scheduleThumbnails);
    connect(m_document, &DocumentController::currentFrameChanged, this, scheduleThumbnails);
    rebuildList();
}

template<typename Edit>
void LayersPanel::editCurrentLayer(Edit edit)
{
    const doc::Layer *layer = m_document->document().layer(m_document->currentLayer());
    if (!layer)
        return;
    doc::LayerProperties props = layer->properties();
    edit(props);
    m_document->execute(doc::makeSetLayerProperties(m_document->document(), layer->id(), props));
}

void LayersPanel::commitOpacity()
{
    const float opacity = float(m_opacity->value()) / 100.0f;
    editCurrentLayer([opacity](doc::LayerProperties &p) { p.opacity = opacity; });
}

void LayersPanel::renameLayer(doc::LayerId id)
{
    const doc::Layer *layer = m_document->document().layer(id);
    if (!layer)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename layer"), tr("Name:"), QLineEdit::Normal,
                                               QString::fromStdString(layer->properties().name), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    doc::LayerProperties props = layer->properties();
    props.name = name.trimmed().toStdString();
    m_document->execute(doc::makeSetLayerProperties(m_document->document(), id, props));
}

void LayersPanel::rebuildList()
{
    const QSignalBlocker block(m_list);
    m_list->clear();
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    const int frame = m_document->currentFrame();

    for (int i = d.layerCount() - 1; i >= 0; --i) { // top layer first
        const doc::Layer &layer = d.layerAt(i);
        const doc::LayerProperties &props = layer.properties();
        const doc::LayerId id = layer.id();

        auto *row = new QWidget(m_list);
        auto *eye = toggleButton(IconId::Eye, tr("Show / hide"), row);
        eye->setChecked(true); // set the icon via toggled
        eye->setChecked(props.visible);
        auto *lock = toggleButton(IconId::Lock, tr("Lock / unlock"), row);
        lock->setChecked(true);
        lock->setChecked(props.locked);
        auto *thumb = new QLabel(row);
        thumb->setObjectName(QStringLiteral("thumbnail"));
        thumb->setPixmap(thumbnail(layer.drawingAt(frame), docSize, nullptr));
        auto *text = new QLabel(QStringLiteral("%1 % %2\n%3")
                                    .arg(std::lround(props.opacity * 100))
                                    .arg(blendName(props.blend), QString::fromStdString(props.name)),
                                row);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(2, 2, 2, 2);
        h->setSpacing(4);
        h->addWidget(eye);
        h->addWidget(lock);
        h->addWidget(thumb);
        h->addWidget(text, 1);

        connect(eye, &QToolButton::clicked, this, [this, id](bool visible) {
            const doc::Layer *l = m_document->document().layer(id);
            doc::LayerProperties p = l->properties();
            p.visible = visible;
            m_document->execute(doc::makeSetLayerProperties(m_document->document(), id, p));
        });
        connect(lock, &QToolButton::clicked, this, [this, id](bool locked) {
            const doc::Layer *l = m_document->document().layer(id);
            doc::LayerProperties p = l->properties();
            p.locked = locked;
            m_document->execute(doc::makeSetLayerProperties(m_document->document(), id, p));
        });

        auto *item = new QListWidgetItem(m_list);
        item->setData(kLayerIdRole, QVariant::fromValue(id));
        item->setSizeHint(row->sizeHint());
        m_list->setItemWidget(item, row);
    }

    // The paper: always at the bottom, not a layer of the model, not selectable.
    auto *paperRow = new QWidget(m_list);
    const QColor paper = QColor::fromRgba(d.paperColor());
    auto *paperThumb = new QLabel(paperRow);
    paperThumb->setPixmap(thumbnail(nullptr, docSize, &paper));
    auto *h = new QHBoxLayout(paperRow);
    h->setContentsMargins(42, 2, 2, 2);
    h->setSpacing(4);
    h->addWidget(paperThumb);
    h->addWidget(new QLabel(tr("Paper"), paperRow), 1);
    auto *paperItem = new QListWidgetItem(m_list);
    paperItem->setFlags(Qt::NoItemFlags);
    paperItem->setSizeHint(paperRow->sizeHint());
    m_list->setItemWidget(paperItem, paperRow);

    selectCurrentRow();
    syncHeader();
}

void LayersPanel::refreshThumbnails()
{
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (!item->data(kLayerIdRole).isValid())
            continue;
        const doc::Layer *layer = d.layer(item->data(kLayerIdRole).value<doc::LayerId>());
        auto *thumb = m_list->itemWidget(item)->findChild<QLabel *>(QStringLiteral("thumbnail"));
        if (layer && thumb)
            thumb->setPixmap(thumbnail(layer->drawingAt(m_document->currentFrame()), docSize, nullptr));
    }
}

void LayersPanel::selectCurrentRow()
{
    const QSignalBlocker block(m_list);
    for (int row = 0; row < m_list->count(); ++row) {
        const QVariant id = m_list->item(row)->data(kLayerIdRole);
        if (id.isValid() && id.value<doc::LayerId>() == m_document->currentLayer()) {
            m_list->setCurrentRow(row);
            return;
        }
    }
}

void LayersPanel::syncHeader()
{
    const doc::Document &d = m_document->document();
    const doc::Layer *layer = d.layer(m_document->currentLayer());
    const int index = layer ? d.indexOf(layer->id()) : -1;

    for (QWidget *w : std::initializer_list<QWidget *>{m_blend, m_opacity, m_opacityValue})
        w->setEnabled(layer != nullptr);
    m_remove->setEnabled(layer && d.layerCount() > 1); // keep at least one layer
    m_up->setEnabled(layer && index + 1 < d.layerCount());
    m_down->setEnabled(layer && index > 0);
    if (!layer)
        return;

    const QSignalBlocker b1(m_blend), b2(m_opacity), b3(m_opacityValue);
    const doc::LayerProperties &props = layer->properties();
    m_blend->setCurrentIndex(int(std::find(std::begin(kBlendModes), std::end(kBlendModes), props.blend)
                                 - std::begin(kBlendModes)));
    const int opacity = int(std::lround(props.opacity * 100));
    m_opacity->setValue(opacity);
    m_opacityValue->setValue(opacity);
}

} // namespace mothion::ui
