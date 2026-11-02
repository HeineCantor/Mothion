#include "ui/panels/LayersPanel.h"

#include "canvas/TileQt.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/LayerCommands.h"
#include "document/DocumentController.h"
#include "ui/IconFactory.h"
#include "ui/panels/BlendModes.h"

#include <QComboBox>
#include <QDrag>
#include <QDropEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace mothion::ui {

using document::DocumentController;

namespace {

constexpr QSize kThumbSize(40, 24);
constexpr int kLayerIdRole = Qt::UserRole + 1;

// Thumbnail of a layer's pixels: checkerboard (or paper color) background,
// tiles scaled down without building a full image.
QPixmap thumbnail(const doc::TiledImage *image, const QSize &docSize, const QColor *paper)
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
    if (image) {
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.scale(double(kThumbSize.width()) / docSize.width(), double(kThumbSize.height()) / docSize.height());
        for (const doc::TileCoord &c : image->tileCoords())
            p.drawImage(canvas::tileRect(c).topLeft(), canvas::tileImage(*image->tile(c)));
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

constexpr double kDragPreviewOpacity = 0.75; // the rows following the pen
constexpr double kDraggedRowOpacity = 0.4;   // the rows left in the list meanwhile

// The rows are widgets laid over empty items, which Qt's own drag preview
// would draw (blank): the drag starts here instead, with a picture of them.
class LayerList : public QListWidget
{
public:
    LayerList(std::function<QPixmap()> preview, QWidget *parent)
        : QListWidget(parent)
        , m_preview(std::move(preview))
    {
    }

protected:
    void startDrag(Qt::DropActions actions) override
    {
        const QModelIndexList indexes = selectedIndexes();
        if (indexes.isEmpty())
            return;
        auto *drag = new QDrag(this);
        drag->setMimeData(model()->mimeData(indexes)); // what the view accepts as a drop
        const QPixmap preview = m_preview();
        drag->setPixmap(preview);
        // Held where the pen is, from the top of the topmost row.
        int top = visualRect(indexes.first()).top();
        for (const QModelIndex &i : indexes)
            top = std::min(top, visualRect(i).top());
        const QPoint grab = viewport()->mapFromGlobal(QCursor::pos()) - QPoint(0, top);
        const QSize size = preview.deviceIndependentSize().toSize();
        drag->setHotSpot(QPoint(std::clamp(grab.x(), 0, size.width() - 1), std::clamp(grab.y(), 0, size.height() - 1)));

        // The rows that go are faded where they were until the drop. A drop
        // rebuilds the rows (maybe before exec() returns): only those still there.
        QList<QPointer<QWidget>> faded;
        for (const QModelIndex &i : indexes) {
            if (QWidget *row = indexWidget(i)) {
                auto *effect = new QGraphicsOpacityEffect(row);
                effect->setOpacity(kDraggedRowOpacity);
                row->setGraphicsEffect(effect);
                faded << row;
            }
        }
        drag->exec(actions, Qt::MoveAction);
        for (const QPointer<QWidget> &row : faded)
            if (row)
                row->setGraphicsEffect(nullptr);
    }

private:
    std::function<QPixmap()> m_preview;
};

} // namespace

LayersPanel::LayersPanel(DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_blend(new QComboBox(this))
    , m_opacity(new QSlider(Qt::Horizontal, this))
    , m_opacityValue(new QSpinBox(this))
    , m_list(new LayerList([this] { return dragPreview(); }, this))
    , m_add(actionButton(IconId::Add, tr("New layer"), this))
    , m_remove(actionButton(IconId::Delete, tr("Delete layer"), this))
    , m_up(actionButton(IconId::ArrowUp, tr("Move up"), this))
    , m_down(actionButton(IconId::ArrowDown, tr("Move down"), this))
{
    for (doc::BlendMode mode : kBlendModes)
        m_blend->addItem(blendModeName(mode));
    m_opacity->setRange(0, 100);
    m_opacityValue->setRange(0, 100);
    m_opacityValue->setSuffix(QStringLiteral(" %"));
    m_opacityValue->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_opacityValue->setKeyboardTracking(false);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Rows are dragged with Qt's drag and drop, but the drop is ours (see
    // eventFilter): the layers move in the drawing and the rows are rebuilt.
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setDropIndicatorShown(true);
    m_list->viewport()->installEventFilter(this);
    m_remove->setToolTip(tr("Delete the selected layers"));
    m_up->setToolTip(tr("Move the selected layers up"));
    m_down->setToolTip(tr("Move the selected layers down"));

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
    layout->addLayout(buttons);
    layout->addWidget(m_list, 1);

    // Header edits.
    connect(m_blend, &QComboBox::activated, this, [this](int index) {
        editSelectedLayers([index](doc::LayerProperties &p) { p.blend = kBlendModes[std::size_t(index)]; });
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
        const doc::Drawing *drawing = m_document->currentDrawing();
        if (!drawing)
            return;
        // Above the current one, named with the first free number.
        QString name;
        for (int n = int(drawing->layers.size()) + 1;; ++n) {
            name = tr("Layer %1").arg(n);
            if (std::none_of(drawing->layers.begin(), drawing->layers.end(),
                             [&](const doc::Layer &l) { return l.properties.name == name.toStdString(); }))
                break;
        }
        auto add = std::make_unique<doc::AddLayerCommand>(
            m_document->documentForCommands(), m_document->currentCel(), drawing->id,
            drawing->indexOf(m_document->currentLayer()) + 1, doc::LayerProperties{name.toStdString()});
        const doc::LayerId id = add->layerId();
        m_document->execute(std::move(add));
        m_document->setCurrentLayer(id);
    });
    connect(m_remove, &QToolButton::clicked, this, &LayersPanel::removeSelected);
    connect(m_up, &QToolButton::clicked, this, [this] { moveSelected(+1); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveSelected(-1); });

    // Selection and renaming.
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && item->data(kLayerIdRole).isValid())
            m_document->setCurrentLayer(item->data(kLayerIdRole).value<doc::LayerId>());
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        m_selected.clear();
        const QList<QListWidgetItem *> selected = m_list->selectedItems();
        for (QListWidgetItem *item : selected)
            if (item->data(kLayerIdRole).isValid())
                m_selected << item->data(kLayerIdRole).value<doc::LayerId>();
        // Ctrl+click took the current layer out: another selected one takes its place.
        QListWidgetItem *current = m_list->currentItem();
        if (current && !current->isSelected() && !selected.isEmpty()) {
            {
                const QSignalBlocker block(m_list);
                m_list->setCurrentItem(selected.last(), QItemSelectionModel::NoUpdate);
            }
            m_document->setCurrentLayer(selected.last()->data(kLayerIdRole).value<doc::LayerId>());
        }
        syncHeader();
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
    // Another cel or frame may show another drawing: its layers.
    auto drawingMayChange = [this] {
        if (shownDrawing() != m_shown)
            rebuildList();
    };
    connect(m_document, &DocumentController::currentCelChanged, this, drawingMayChange);
    connect(m_document, &DocumentController::currentFrameChanged, this, drawingMayChange);
    connect(m_document, &DocumentController::documentReplaced, this, &LayersPanel::rebuildList);
    connect(m_document, &DocumentController::pixelsChanged, this, scheduleThumbnails);
    rebuildList();
}

QPixmap LayersPanel::dragPreview() const
{
    // The selected rows as shown, top first, stacked, on the selection color.
    QList<QPixmap> rows;
    int width = 0, height = 0;
    for (int r = 0; r < m_list->count(); ++r) {
        QListWidgetItem *item = m_list->item(r);
        QWidget *row = m_list->itemWidget(item);
        if (!item->isSelected() || !row)
            continue;
        rows << row->grab();
        width = std::max(width, row->width());
        height += row->height();
    }
    if (rows.isEmpty())
        return {};
    const qreal ratio = devicePixelRatioF();
    QPixmap preview(QSize(width, height) * ratio);
    preview.setDevicePixelRatio(ratio);
    preview.fill(Qt::transparent);
    QPainter p(&preview);
    p.setOpacity(kDragPreviewOpacity);
    p.fillRect(QRect(0, 0, width, height), palette().color(QPalette::Highlight));
    int y = 0;
    for (const QPixmap &row : rows) {
        p.drawPixmap(0, y, row);
        y += row.deviceIndependentSize().toSize().height();
    }
    return preview;
}

LayersPanel::Shown LayersPanel::shownDrawing() const
{
    return {m_document->currentCel(), m_document->currentDrawingId()};
}

QList<doc::LayerId> LayersPanel::selectedLayers() const
{
    QList<doc::LayerId> layers;
    const doc::Drawing *drawing = m_document->currentDrawing();
    if (!drawing)
        return layers;
    for (const doc::Layer &layer : drawing->layers)
        if (m_selected.contains(layer.id) || layer.id == m_document->currentLayer())
            layers << layer.id;
    return layers;
}

template<typename Edit>
void LayersPanel::editSelectedLayers(Edit edit)
{
    const doc::Drawing *drawing = m_document->currentDrawing();
    if (!drawing)
        return;
    const doc::CelId cel = m_document->currentCel();
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (doc::LayerId id : selectedLayers()) {
        doc::LayerProperties props = drawing->layer(id)->properties;
        edit(props);
        if (auto step = doc::makeSetLayerProperties(m_document->document(), cel, drawing->id, id, props))
            steps.push_back(std::move(step));
    }
    if (steps.size() == 1)
        m_document->execute(std::move(steps.front()));
    else if (!steps.empty())
        m_document->execute(
            std::make_unique<doc::GroupCommand>(doc::CommandKind::ChangeLayerProperties, std::move(steps)));
}

void LayersPanel::commitOpacity()
{
    const float opacity = float(m_opacity->value()) / 100.0f;
    editSelectedLayers([opacity](doc::LayerProperties &p) { p.opacity = opacity; });
}

void LayersPanel::removeSelected()
{
    const doc::Drawing *drawing = m_document->currentDrawing();
    const QList<doc::LayerId> layers = selectedLayers();
    if (!drawing || layers.isEmpty() || layers.size() >= int(drawing->layers.size()))
        return; // keep at least one layer
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (doc::LayerId id : layers)
        steps.push_back(std::make_unique<doc::RemoveLayerCommand>(m_document->currentCel(), drawing->id, id));
    m_selected.clear();
    m_document->execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::RemoveLayer, std::move(steps)));
}

bool LayersPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list->viewport() && event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        if (drop->source() == m_list) {
            const QPoint at = drop->position().toPoint();
            const QModelIndex index = m_list->indexAt(at);
            // Before the row under the pen, or after it in its lower half.
            int row = index.isValid() ? index.row() : m_list->count();
            if (index.isValid() && at.y() > m_list->visualRect(index).center().y())
                ++row;
            // Ignored for Qt, so the view itself neither moves nor removes rows.
            drop->setDropAction(Qt::IgnoreAction);
            drop->accept();
            moveSelectedToRow(row);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LayersPanel::moveSelectedToRow(int row)
{
    const doc::Drawing *drawing = m_document->currentDrawing();
    if (!drawing)
        return;
    const int n = int(drawing->layers.size());
    const QList<doc::LayerId> moving = selectedLayers();
    auto idAt = [drawing](int i) { return drawing->layers[std::size_t(i)].id; };
    // The new stack, top first as the rows: the others, then the moving ones
    // put in before `row` (counted without themselves).
    std::vector<doc::LayerId> rows;
    int insertAt = std::clamp(row, 0, n);
    for (int r = 0; r < n; ++r) {
        const doc::LayerId id = idAt(n - 1 - r);
        if (moving.contains(id)) {
            if (r < row)
                --insertAt;
        } else {
            rows.push_back(id);
        }
    }
    std::vector<doc::LayerId> top(moving.rbegin(), moving.rend()); // top first too
    rows.insert(rows.begin() + std::clamp(insertAt, 0, int(rows.size())), top.begin(), top.end());

    // Moves that turn the stack into it, bottom up.
    std::vector<doc::LayerId> order;
    for (int i = 0; i < n; ++i)
        order.push_back(idAt(i));
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (int i = 0; i < n; ++i) {
        const doc::LayerId want = rows[std::size_t(n - 1 - i)];
        if (order[std::size_t(i)] == want)
            continue;
        steps.push_back(std::make_unique<doc::MoveLayerCommand>(m_document->currentCel(), drawing->id, want, i));
        order.erase(std::find(order.begin(), order.end(), want));
        order.insert(order.begin() + i, want);
    }
    if (!steps.empty())
        m_document->execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::MoveLayer, std::move(steps)));
}

void LayersPanel::moveSelected(int direction)
{
    // The stack as it will be, step by step: a selected layer swaps with an
    // unselected neighbour; one that would bump into the end, or into a
    // selected layer that cannot move, stays (the block keeps its gaps).
    const doc::Drawing *drawing = m_document->currentDrawing();
    if (!drawing)
        return;
    const QList<doc::LayerId> selected = selectedLayers();
    std::vector<doc::LayerId> order;
    for (const doc::Layer &layer : drawing->layers)
        order.push_back(layer.id);
    std::vector<std::unique_ptr<doc::Command>> steps;
    const int n = int(order.size());
    for (int k = 0; k < n; ++k) {
        const int i = direction > 0 ? n - 1 - k : k; // the end it moves to first
        const int to = i + direction;
        if (to < 0 || to >= n || !selected.contains(order[std::size_t(i)]) || selected.contains(order[std::size_t(to)]))
            continue;
        steps.push_back(
            std::make_unique<doc::MoveLayerCommand>(m_document->currentCel(), drawing->id, order[std::size_t(i)], to));
        std::swap(order[std::size_t(i)], order[std::size_t(to)]);
    }
    if (!steps.empty())
        m_document->execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::MoveLayer, std::move(steps)));
}

void LayersPanel::renameLayer(doc::LayerId id)
{
    const doc::Drawing *drawing = m_document->currentDrawing();
    const doc::Layer *layer = drawing ? drawing->layer(id) : nullptr;
    if (!layer)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename layer"), tr("Name:"), QLineEdit::Normal,
                                               QString::fromStdString(layer->properties.name), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    doc::LayerProperties props = layer->properties;
    props.name = name.trimmed().toStdString();
    m_document->execute(
        doc::makeSetLayerProperties(m_document->document(), m_document->currentCel(), drawing->id, id, props));
}

void LayersPanel::rebuildList()
{
    const QSignalBlocker block(m_list);
    m_list->clear();
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    const doc::Drawing *drawing = m_document->currentDrawing();
    m_shown = shownDrawing();

    if (!drawing) {
        auto *empty = new QListWidgetItem(tr("Empty frame: draw on it, or make a new frame"), m_list);
        empty->setFlags(Qt::NoItemFlags);
        empty->setTextAlignment(Qt::AlignCenter);
    }
    const doc::CelId cel = m_document->currentCel();
    const int count = drawing ? int(drawing->layers.size()) : 0;
    for (int i = count - 1; i >= 0; --i) { // top layer first
        const doc::Layer &layer = drawing->layers[std::size_t(i)];
        const doc::LayerProperties &props = layer.properties;
        const doc::LayerId id = layer.id;
        const doc::DrawingId drawingId = drawing->id;

        auto *row = new QWidget(m_list);
        auto *eye = toggleButton(IconId::Eye, tr("Show / hide"), row);
        eye->setChecked(true); // set the icon via toggled
        eye->setChecked(props.visible);
        auto *lock = toggleButton(IconId::Lock, tr("Lock / unlock"), row);
        lock->setChecked(true);
        lock->setChecked(props.locked);
        auto *thumb = new QLabel(row);
        thumb->setObjectName(QStringLiteral("thumbnail"));
        thumb->setPixmap(thumbnail(&layer.image, docSize, nullptr));
        auto *text = new QLabel(QStringLiteral("%1 % %2\n%3")
                                    .arg(std::lround(props.opacity * 100))
                                    .arg(blendModeName(props.blend), QString::fromStdString(props.name)),
                                row);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(2, 2, 2, 2);
        h->setSpacing(4);
        h->addWidget(eye);
        h->addWidget(lock);
        h->addWidget(thumb);
        h->addWidget(text, 1);

        auto toggle = [this, cel, drawingId, id](auto change) {
            const doc::Layer *l = m_document->document().layer(cel, drawingId, id);
            if (!l)
                return;
            doc::LayerProperties p = l->properties;
            change(p);
            m_document->execute(doc::makeSetLayerProperties(m_document->document(), cel, drawingId, id, p));
        };
        connect(eye, &QToolButton::clicked, this,
                [toggle](bool visible) { toggle([visible](doc::LayerProperties &p) { p.visible = visible; }); });
        connect(lock, &QToolButton::clicked, this,
                [toggle](bool locked) { toggle([locked](doc::LayerProperties &p) { p.locked = locked; }); });

        auto *item = new QListWidgetItem(m_list);
        item->setData(kLayerIdRole, QVariant::fromValue(id));
        item->setSizeHint(row->sizeHint());
        m_list->setItemWidget(item, row);
    }

    // The paper: always at the bottom, not a layer of the drawing, not selectable.
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

    restoreSelection();
    syncHeader();
}

void LayersPanel::refreshThumbnails()
{
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    const doc::Drawing *drawing = m_document->currentDrawing();
    for (int row = 0; drawing && row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (!item->data(kLayerIdRole).isValid())
            continue;
        const doc::Layer *layer = drawing->layer(item->data(kLayerIdRole).value<doc::LayerId>());
        auto *thumb = m_list->itemWidget(item)->findChild<QLabel *>(QStringLiteral("thumbnail"));
        if (layer && thumb)
            thumb->setPixmap(thumbnail(&layer->image, docSize, nullptr));
    }
}

void LayersPanel::selectCurrentRow()
{
    // The current layer chosen here (a click) keeps the selection made with
    // it; chosen elsewhere it becomes the only one selected.
    const QListWidgetItem *current = m_list->currentItem();
    if (current && current->data(kLayerIdRole).isValid()
        && current->data(kLayerIdRole).value<doc::LayerId>() == m_document->currentLayer())
        return;
    const QSignalBlocker block(m_list);
    m_selected.clear();
    for (int row = 0; row < m_list->count(); ++row) {
        const QVariant id = m_list->item(row)->data(kLayerIdRole);
        if (id.isValid() && id.value<doc::LayerId>() == m_document->currentLayer()) {
            m_list->setCurrentRow(row); // and only it selected
            m_selected << id.value<doc::LayerId>();
            return;
        }
    }
}

void LayersPanel::restoreSelection()
{
    // After a rebuild: the layers selected before that still exist, and the
    // current one (as current, without touching the rest).
    const QSignalBlocker block(m_list);
    QList<doc::LayerId> kept;
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        const QVariant id = item->data(kLayerIdRole);
        if (!id.isValid())
            continue;
        const doc::LayerId layer = id.value<doc::LayerId>();
        const bool current = layer == m_document->currentLayer();
        if (current)
            m_list->setCurrentItem(item, QItemSelectionModel::NoUpdate);
        if (current || m_selected.contains(layer)) {
            item->setSelected(true);
            kept << layer;
        }
    }
    m_selected = kept;
}

void LayersPanel::syncHeader()
{
    const doc::Drawing *drawing = m_document->currentDrawing();
    const doc::Layer *layer = drawing ? drawing->layer(m_document->currentLayer()) : nullptr;

    for (QWidget *w : std::initializer_list<QWidget *>{m_blend, m_opacity, m_opacityValue})
        w->setEnabled(layer != nullptr);
    // Up and down while some selected layer can still go that way.
    const QList<doc::LayerId> selected = selectedLayers();
    const int n = drawing ? int(drawing->layers.size()) : 0;
    bool canUp = false, canDown = false;
    for (doc::LayerId id : selected) {
        const int i = drawing->indexOf(id);
        canUp = canUp || (i + 1 < n && !selected.contains(drawing->layers[std::size_t(i + 1)].id));
        canDown = canDown || (i > 0 && !selected.contains(drawing->layers[std::size_t(i - 1)].id));
    }
    m_add->setEnabled(drawing != nullptr);
    m_remove->setEnabled(layer && selected.size() < n); // keep at least one layer
    m_up->setEnabled(layer && canUp);
    m_down->setEnabled(layer && canDown);
    if (!layer)
        return;

    const QSignalBlocker b1(m_blend), b2(m_opacity), b3(m_opacityValue);
    const doc::LayerProperties &props = layer->properties;
    m_blend->setCurrentIndex(blendModeIndex(props.blend));
    const int opacity = int(std::lround(props.opacity * 100));
    m_opacity->setValue(opacity);
    m_opacityValue->setValue(opacity);
}

} // namespace mothion::ui
