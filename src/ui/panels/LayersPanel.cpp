#include "ui/panels/LayersPanel.h"

#include "canvas/FrameRenderer.h"
#include "canvas/TileQt.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/CelCommands.h"
#include "document/DocumentController.h"
#include "ui/IconFactory.h"

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
constexpr int kCelIdRole = Qt::UserRole + 1;

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
        const QRect bounds = canvas::drawingBounds(*drawing);
        p.drawImage(bounds.topLeft(), canvas::composeDrawing(*drawing, bounds));
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
    , m_add(actionButton(IconId::Add, tr("New cel"), this))
    , m_remove(actionButton(IconId::Delete, tr("Delete cel"), this))
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
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Rows are dragged with Qt's drag and drop, but the drop is ours (see
    // eventFilter): the layers move in the document and the rows are rebuilt.
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setDropIndicatorShown(true);
    m_list->viewport()->installEventFilter(this);
    m_remove->setToolTip(tr("Delete the selected cels"));
    m_up->setToolTip(tr("Move the selected cels up"));
    m_down->setToolTip(tr("Move the selected cels down"));

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
        editSelectedCels([index](doc::CelProperties &p) { p.blend = kBlendModes[index]; });
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
        const int index = d.indexOf(m_document->currentCel()) + 1; // above the current one
        auto add = std::make_unique<doc::AddCelCommand>(
            d, index, doc::CelProperties{tr("Cel %1").arg(d.celCount() + 1).toStdString()});
        const doc::CelId id = add->celId();
        m_document->execute(std::move(add));
        m_document->setCurrentCel(id);
    });
    connect(m_remove, &QToolButton::clicked, this, &LayersPanel::removeSelected);
    connect(m_up, &QToolButton::clicked, this, [this] { moveSelected(+1); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveSelected(-1); });

    // Selection and renaming.
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item && item->data(kCelIdRole).isValid())
            m_document->setCurrentCel(item->data(kCelIdRole).value<doc::CelId>());
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        m_selected.clear();
        const QList<QListWidgetItem *> selected = m_list->selectedItems();
        for (QListWidgetItem *item : selected)
            m_selected << item->data(kCelIdRole).value<doc::CelId>();
        // Ctrl+click took the current layer out: another selected one takes its place.
        QListWidgetItem *current = m_list->currentItem();
        if (current && !current->isSelected() && !selected.isEmpty()) {
            {
                const QSignalBlocker block(m_list);
                m_list->setCurrentItem(selected.last(), QItemSelectionModel::NoUpdate);
            }
            m_document->setCurrentCel(selected.last()->data(kCelIdRole).value<doc::CelId>());
        }
        syncHeader();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item->data(kCelIdRole).isValid())
            renameCel(item->data(kCelIdRole).value<doc::CelId>());
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
    connect(m_document, &DocumentController::currentCelChanged, this, [this] {
        selectCurrentRow();
        syncHeader();
    });
    connect(m_document, &DocumentController::pixelsChanged, this, scheduleThumbnails);
    connect(m_document, &DocumentController::currentFrameChanged, this, scheduleThumbnails);
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

QList<doc::CelId> LayersPanel::selectedCels() const
{
    const doc::Document &d = m_document->document();
    QList<doc::CelId> cels;
    for (int i = 0; i < d.celCount(); ++i) {
        const doc::CelId id = d.celAt(i).id();
        if (m_selected.contains(id) || id == m_document->currentCel())
            cels << id;
    }
    return cels;
}

template<typename Edit>
void LayersPanel::editSelectedCels(Edit edit)
{
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (doc::CelId id : selectedCels()) {
        doc::CelProperties props = m_document->document().cel(id)->properties();
        edit(props);
        if (auto step = doc::makeSetCelProperties(m_document->document(), id, props))
            steps.push_back(std::move(step));
    }
    if (steps.size() == 1)
        m_document->execute(std::move(steps.front()));
    else if (!steps.empty())
        m_document->execute(
            std::make_unique<doc::GroupCommand>(doc::CommandKind::ChangeCelProperties, std::move(steps)));
}

void LayersPanel::commitOpacity()
{
    const float opacity = float(m_opacity->value()) / 100.0f;
    editSelectedCels([opacity](doc::CelProperties &p) { p.opacity = opacity; });
}

void LayersPanel::removeSelected()
{
    const QList<doc::CelId> cels = selectedCels();
    if (cels.isEmpty() || cels.size() >= m_document->document().celCount())
        return; // keep at least one layer
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (doc::CelId id : cels)
        steps.push_back(std::make_unique<doc::RemoveCelCommand>(id));
    m_selected.clear();
    m_document->execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::RemoveCel, std::move(steps)));
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
    const doc::Document &d = m_document->document();
    const int n = d.celCount();
    const QList<doc::CelId> moving = selectedCels();
    // The new stack, top first as the rows: the others, then the moving ones
    // put in before `row` (counted without themselves).
    std::vector<doc::CelId> rows;
    int insertAt = std::clamp(row, 0, n);
    for (int r = 0; r < n; ++r) {
        const doc::CelId id = d.celAt(n - 1 - r).id();
        if (moving.contains(id)) {
            if (r < row)
                --insertAt;
        } else {
            rows.push_back(id);
        }
    }
    std::vector<doc::CelId> top(moving.rbegin(), moving.rend()); // top first too
    rows.insert(rows.begin() + std::clamp(insertAt, 0, int(rows.size())), top.begin(), top.end());

    // Moves that turn the stack into it, bottom up.
    std::vector<doc::CelId> order;
    for (int i = 0; i < n; ++i)
        order.push_back(d.celAt(i).id());
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (int i = 0; i < n; ++i) {
        const doc::CelId want = rows[std::size_t(n - 1 - i)];
        if (order[std::size_t(i)] == want)
            continue;
        steps.push_back(std::make_unique<doc::MoveCelCommand>(want, i));
        order.erase(std::find(order.begin(), order.end(), want));
        order.insert(order.begin() + i, want);
    }
    if (!steps.empty())
        m_document->execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::MoveCel, std::move(steps)));
}

void LayersPanel::moveSelected(int direction)
{
    // The stack as it will be, step by step: a selected layer swaps with an
    // unselected neighbour; one that would bump into the end, or into a
    // selected layer that cannot move, stays (the block keeps its gaps).
    const doc::Document &d = m_document->document();
    const QList<doc::CelId> selected = selectedCels();
    std::vector<doc::CelId> order;
    for (int i = 0; i < d.celCount(); ++i)
        order.push_back(d.celAt(i).id());
    std::vector<std::unique_ptr<doc::Command>> steps;
    const int n = int(order.size());
    for (int k = 0; k < n; ++k) {
        const int i = direction > 0 ? n - 1 - k : k; // the end it moves to first
        const int to = i + direction;
        if (to < 0 || to >= n || !selected.contains(order[std::size_t(i)]) || selected.contains(order[std::size_t(to)]))
            continue;
        steps.push_back(std::make_unique<doc::MoveCelCommand>(order[std::size_t(i)], to));
        std::swap(order[std::size_t(i)], order[std::size_t(to)]);
    }
    if (!steps.empty())
        m_document->execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::MoveCel, std::move(steps)));
}

void LayersPanel::renameCel(doc::CelId id)
{
    const doc::Cel *cel = m_document->document().cel(id);
    if (!cel)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename cel"), tr("Name:"), QLineEdit::Normal,
                                               QString::fromStdString(cel->properties().name), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    doc::CelProperties props = cel->properties();
    props.name = name.trimmed().toStdString();
    m_document->execute(doc::makeSetCelProperties(m_document->document(), id, props));
}

void LayersPanel::rebuildList()
{
    const QSignalBlocker block(m_list);
    m_list->clear();
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    const int frame = m_document->currentFrame();

    for (int i = d.celCount() - 1; i >= 0; --i) { // top layer first
        const doc::Cel &cel = d.celAt(i);
        const doc::CelProperties &props = cel.properties();
        const doc::CelId id = cel.id();

        auto *row = new QWidget(m_list);
        auto *eye = toggleButton(IconId::Eye, tr("Show / hide"), row);
        eye->setChecked(true); // set the icon via toggled
        eye->setChecked(props.visible);
        auto *lock = toggleButton(IconId::Lock, tr("Lock / unlock"), row);
        lock->setChecked(true);
        lock->setChecked(props.locked);
        auto *thumb = new QLabel(row);
        thumb->setObjectName(QStringLiteral("thumbnail"));
        thumb->setPixmap(thumbnail(cel.drawingAt(frame), docSize, nullptr));
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
            const doc::Cel *l = m_document->document().cel(id);
            doc::CelProperties p = l->properties();
            p.visible = visible;
            m_document->execute(doc::makeSetCelProperties(m_document->document(), id, p));
        });
        connect(lock, &QToolButton::clicked, this, [this, id](bool locked) {
            const doc::Cel *l = m_document->document().cel(id);
            doc::CelProperties p = l->properties();
            p.locked = locked;
            m_document->execute(doc::makeSetCelProperties(m_document->document(), id, p));
        });

        auto *item = new QListWidgetItem(m_list);
        item->setData(kCelIdRole, QVariant::fromValue(id));
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

    restoreSelection();
    syncHeader();
}

void LayersPanel::refreshThumbnails()
{
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (!item->data(kCelIdRole).isValid())
            continue;
        const doc::Cel *cel = d.cel(item->data(kCelIdRole).value<doc::CelId>());
        auto *thumb = m_list->itemWidget(item)->findChild<QLabel *>(QStringLiteral("thumbnail"));
        if (cel && thumb)
            thumb->setPixmap(thumbnail(cel->drawingAt(m_document->currentFrame()), docSize, nullptr));
    }
}

void LayersPanel::selectCurrentRow()
{
    // The current layer chosen here (a click) keeps the selection made with
    // it; chosen elsewhere it becomes the only one selected.
    const QListWidgetItem *current = m_list->currentItem();
    if (current && current->data(kCelIdRole).isValid()
        && current->data(kCelIdRole).value<doc::CelId>() == m_document->currentCel())
        return;
    const QSignalBlocker block(m_list);
    m_selected.clear();
    for (int row = 0; row < m_list->count(); ++row) {
        const QVariant id = m_list->item(row)->data(kCelIdRole);
        if (id.isValid() && id.value<doc::CelId>() == m_document->currentCel()) {
            m_list->setCurrentRow(row); // and only it selected
            m_selected << id.value<doc::CelId>();
            return;
        }
    }
}

void LayersPanel::restoreSelection()
{
    // After a rebuild: the layers selected before that still exist, and the
    // current one (as current, without touching the rest).
    const QSignalBlocker block(m_list);
    QList<doc::CelId> kept;
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        const QVariant id = item->data(kCelIdRole);
        if (!id.isValid())
            continue;
        const doc::CelId cel = id.value<doc::CelId>();
        const bool current = cel == m_document->currentCel();
        if (current)
            m_list->setCurrentItem(item, QItemSelectionModel::NoUpdate);
        if (current || m_selected.contains(cel)) {
            item->setSelected(true);
            kept << cel;
        }
    }
    m_selected = kept;
}

void LayersPanel::syncHeader()
{
    const doc::Document &d = m_document->document();
    const doc::Cel *cel = d.cel(m_document->currentCel());

    for (QWidget *w : std::initializer_list<QWidget *>{m_blend, m_opacity, m_opacityValue})
        w->setEnabled(cel != nullptr);
    // Up and down while some selected layer can still go that way.
    const QList<doc::CelId> selected = selectedCels();
    bool canUp = false, canDown = false;
    for (doc::CelId id : selected) {
        const int i = d.indexOf(id);
        canUp = canUp || (i + 1 < d.celCount() && !selected.contains(d.celAt(i + 1).id()));
        canDown = canDown || (i > 0 && !selected.contains(d.celAt(i - 1).id()));
    }
    m_remove->setEnabled(cel && selected.size() < d.celCount()); // keep at least one layer
    m_up->setEnabled(cel && canUp);
    m_down->setEnabled(cel && canDown);
    if (!cel)
        return;

    const QSignalBlocker b1(m_blend), b2(m_opacity), b3(m_opacityValue);
    const doc::CelProperties &props = cel->properties();
    m_blend->setCurrentIndex(int(std::find(std::begin(kBlendModes), std::end(kBlendModes), props.blend)
                                 - std::begin(kBlendModes)));
    const int opacity = int(std::lround(props.opacity * 100));
    m_opacity->setValue(opacity);
    m_opacityValue->setValue(opacity);
}

} // namespace mothion::ui
