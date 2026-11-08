#include "ui/panels/LayersPanel.h"

#include "canvas/FrameRenderer.h"
#include "canvas/TileQt.h"
#include "core/document/commands/CelCommands.h"
#include "core/document/commands/DrawingCommands.h"
#include "core/document/commands/GroupCommand.h"
#include "core/document/commands/LayerCommands.h"
#include "document/DocumentController.h"
#include "tools/ToolState.h"
#include "ui/IconFactory.h"
#include "ui/Shortcuts.h"
#include "ui/Theme.h"
#include "ui/panels/BlendModes.h"
#include "ui/panels/timeline/TimelineView.h"

#include <QComboBox>
#include <QDrag>
#include <QDropEvent>
#include <QGraphicsOpacityEffect>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QApplication>
#include <QEvent>
#include <QStyle>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <cmath>
#include <functional>
#include <limits>

namespace mothion::ui {

using document::DocumentController;

namespace {

constexpr QSize kThumbSize(40, 24);

// What a row is, and whose.
constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kCelRole = Qt::UserRole + 2;
constexpr int kLayerRole = Qt::UserRole + 3;
constexpr int kDrawingRole = Qt::UserRole + 4;
enum class Row { Cel, Frame, Layer, Paper, Camera };

Row kindOf(const QTreeWidgetItem *item)
{
    return Row(item->data(0, kKindRole).toInt());
}

doc::CelId celOf(const QTreeWidgetItem *item)
{
    return item->data(0, kCelRole).value<doc::CelId>();
}

doc::LayerId layerOf(const QTreeWidgetItem *item)
{
    return item->data(0, kLayerRole).value<doc::LayerId>();
}

doc::DrawingId drawingOf(const QTreeWidgetItem *item)
{
    return item->data(0, kDrawingRole).value<doc::DrawingId>();
}

QString exposureText(const doc::Timing &timing, doc::DrawingId drawing);

// The text of a frame's row: its name (and who else is working on it, in a shared session) and where it is shown.
QString frameLabel(const doc::Cel &cel, const doc::Drawing &drawing, const QString &holder)
{
    const QString name = QCoreApplication::translate("LayersPanel", "Frame %1").arg(QString::fromStdString(drawing.name));
    const QString first = holder.isEmpty() ? name : name + QStringLiteral(" · ") + holder;
    return first + QLatin1Char('\n') + exposureText(cel.timing(), drawing.id);
}

// Where a drawing is shown, 1-based as the timeline counts: "1–2, 5, 9–12".
QString exposureText(const doc::Timing &timing, doc::DrawingId drawing)
{
    QStringList runs;
    for (const doc::Exposure &e : timing.exposures())
        if (e.drawing == drawing)
            runs << (e.length == 1 ? QString::number(e.start + 1)
                                   : QStringLiteral("%1–%2").arg(e.start + 1).arg(e.end()));
    if (runs.isEmpty())
        return LayersPanel::tr("not shown");
    if (runs.size() > 4)
        return runs.mid(0, 4).join(QStringLiteral(", ")) + QStringLiteral(", …");
    return runs.join(QStringLiteral(", "));
}

// Thumbnail of a layer's pixels, or of a drawing's layers composed:
// checkerboard (or paper color) background, scaled down.
QPixmap thumbnail(const doc::TiledImage *image, const doc::Drawing *drawing, const QSize &docSize,
                  const QColor *paper)
{
    QImage img(kThumbSize, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    if (!paper || paper->alpha() < 255) { // squares: behind everything, and behind a see-through paper
        for (int y = 0; y < img.height(); y += 6)
            for (int x = 0; x < img.width(); x += 6)
                p.fillRect(x, y, 6, 6, ((x + y) / 6) % 2 ? QColor(0xcc, 0xcc, 0xcc) : Qt::white);
    }
    if (paper)
        p.fillRect(img.rect(), *paper);
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.scale(double(kThumbSize.width()) / docSize.width(), double(kThumbSize.height()) / docSize.height());
    if (image)
        for (const doc::TileCoord &c : image->tileCoords())
            p.drawImage(canvas::tileRect(c).topLeft(), canvas::tileImage(*image->tile(c)));
    if (drawing) {
        // Only the sheet is shown: a perspective layer's lines reach far out of it.
        const QRect bounds = canvas::drawingBounds(*drawing) & QRect(QPoint(0, 0), docSize);
        p.drawImage(bounds.topLeft(), canvas::composeDrawing(*drawing, bounds));
    }
    p.restore();
    p.setPen(QColor(0x18, 0x18, 0x1a));
    p.drawRect(img.rect().adjusted(0, 0, -1, -1));
    return QPixmap::fromImage(img);
}

// A thumbnail that can be clicked (the click goes on to the row, which it also selects) and outlined (the one being
// drawn on, of a layer and its mask).
class ThumbLabel : public QLabel
{
public:
    explicit ThumbLabel(QWidget *parent) : QLabel(parent) {}
    void setActive(bool active)
    {
        if (active != m_active) {
            m_active = active;
            update();
        }
    }
    std::function<void()> onPress;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (onPress)
            onPress();
        QLabel::mousePressEvent(event); // not taken: the row gets it
    }
    void paintEvent(QPaintEvent *event) override
    {
        QLabel::paintEvent(event);
        if (!m_active)
            return;
        // Around the thumbnail itself (the label may be taller than it, in a taller row), in a dark blue on a light
        // theme, the palette's highlight on a dark one.
        const QPalette app = QApplication::palette();
        const QColor color = app.color(QPalette::Base).lightness() >= 128 ? QColor(0x1f, 0x4f, 0xa8)
                                                                          : app.color(QPalette::Highlight);
        const QRect picture = QStyle::alignedRect(layoutDirection(), alignment(), pixmap().size(), contentsRect());
        QPainter p(this);
        p.setPen(QPen(color, 2));
        p.drawRect(picture.adjusted(1, 1, -1, -1));
    }

private:
    bool m_active = false;
};

// A layer's mask in a thumbnail: white where the layer shows, black where it does not.
QPixmap maskThumbnail(const doc::TiledImage &mask, const QSize &docSize)
{
    const QColor white(Qt::white);
    return thumbnail(&mask, nullptr, docSize, &white);
}

// A layer's thumbnail: its pixels, or a perspective layer's grid drawn small
// (scaled as a grid, so its lines stay visible).
QPixmap layerThumbnail(const doc::Layer &layer, const QSize &docSize)
{
    if (layer.kind != doc::LayerKind::Perspective)
        return thumbnail(&layer.image, nullptr, docSize, nullptr);
    QPixmap pixmap = thumbnail(nullptr, nullptr, docSize, nullptr);
    perspective::Grid small = layer.properties.perspective;
    const double k = std::min(double(kThumbSize.width()) / docSize.width(),
                              double(kThumbSize.height()) / docSize.height());
    // The whole scene smaller: what the camera sees keeps its shape.
    small.originX *= k;
    small.originY *= k;
    small.position = small.position * k;
    small.frameWidth *= k;
    small.cell *= k;
    QPainter p(&pixmap);
    p.setClipRect(pixmap.rect().adjusted(1, 1, -1, -1));
    canvas::paintPerspective(p, small);
    return pixmap;
}

// The glyph of a row's button or label, in the tone of its row (white over the highlight when selected).
void showGlyph(QWidget *w, bool shown = true)
{
    const IconTone tone = w->property("rowSelected").toBool() ? IconTone::Selected : IconTone::Plain;
    const auto id = IconId(w->property("glyph").toInt());
    if (auto *button = qobject_cast<QToolButton *>(w))
        button->setIcon(shown ? icon(id, tone) : QIcon());
    else if (auto *label = qobject_cast<QLabel *>(w))
        label->setPixmap(icon(id, tone).pixmap(16, 16));
}

QToolButton *toggleButton(IconId id, const QString &tip, bool on, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setObjectName(QStringLiteral("LayerEye")); // styled without the "checked" fill
    b->setIconSize(QSize(14, 14));
    b->setCheckable(true);
    b->setAutoRaise(true);
    b->setToolTip(tip);
    // Checked: icon shown; unchecked: empty slot (with its outline). Its color follows the row's selection.
    b->setProperty("glyph", int(id));
    QObject::connect(b, &QToolButton::toggled, b, [b](bool checked) { showGlyph(b, checked); });
    b->setChecked(true); // the icon, via toggled
    b->setChecked(on);
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

QLabel *iconLabel(IconId id, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setProperty("glyph", int(id));
    showGlyph(label);
    return label;
}

// How a frame's layers look, as one string: what their rows show and what their thumbnails are made of.
QString looksOf(const doc::Drawing &drawing)
{
    QString key;
    for (const doc::Layer &layer : drawing.layers) {
        const doc::LayerProperties &p = layer.properties;
        key += QStringLiteral("%1,%2,%3,%4,%5,%6;")
                   .arg(layer.id)
                   .arg(p.visible)
                   .arg(p.locked)
                   .arg(double(p.opacity))
                   .arg(int(p.blend))
                   .arg(QString::fromStdString(p.name));
    }
    return key;
}

QString propertiesText(float opacity, doc::BlendMode blend, const std::string &name)
{
    return QStringLiteral("%1 % %2\n%3")
        .arg(std::lround(opacity * 100))
        .arg(blendModeName(blend), QString::fromStdString(name));
}

constexpr double kDragPreviewOpacity = 0.75; // the rows following the pen
constexpr double kDraggedRowOpacity = 0.4;   // the rows left in the tree meanwhile

// The rows are widgets laid over empty items, which Qt's own drag preview
// would draw (blank): the drag starts here instead, with a picture of them.
class LayerTree : public QTreeWidget
{
public:
    // `strip`: the color of the narrow column at the left of a row (that of its cel, whole when the cel is the chosen
    // one), none for rows that belong to no cel.
    LayerTree(std::function<QList<QTreeWidgetItem *>()> moving, std::function<QPixmap()> preview,
              std::function<QColor(QTreeWidgetItem *)> strip, QWidget *parent)
        : QTreeWidget(parent)
        , m_moving(std::move(moving))
        , m_preview(std::move(preview))
        , m_strip(std::move(strip))
    {
    }

protected:
    // The column at the left (under the expand arrows) is in the color of the row's cel; the selected rows are
    // outlined (the tree's highlight is transparent: see LayersPanel::paintRowColors()).
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QTreeWidget::drawRow(painter, option, index);
        const QRect item = visualRect(index);
        const int contentLeft = visualRect(model()->index(0, 0)).left(); // where the top rows start: the column is left of it
        const bool selected = selectionModel()->isSelected(index);
        QRect bar = item;
        bar.setLeft(contentLeft);
        bar.setRight(viewport()->width() - 1);
        if (selected) {
            // A selected row is drawn with the highlight (transparent) instead of its own background: put that back.
            const QBrush own = index.data(Qt::BackgroundRole).value<QBrush>();
            if (own.style() != Qt::NoBrush)
                painter->fillRect(bar, own);
        }
        const QColor strip = m_strip(itemFromIndex(index));
        if (strip.isValid()) {
            const QRect column(0, item.top(), contentLeft, item.height());
            painter->fillRect(column, strip);
            drawBranches(painter, QRect(0, item.top(), item.left(), item.height()), index); // the arrows, over it
        }
        if (!selected)
            return;
        bar.setLeft(0);
        painter->save();
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(selectionOutlineColor(QApplication::palette()), 2.0));
        painter->drawRect(bar.adjusted(1, 1, -1, -1)); // square, two whole pixels
        painter->restore();
    }

    void startDrag(Qt::DropActions actions) override
    {
        const QList<QTreeWidgetItem *> moving = m_moving();
        if (moving.isEmpty())
            return;
        QModelIndexList indexes;
        for (QTreeWidgetItem *item : moving)
            indexes << indexFromItem(item);
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
        drag->setHotSpot(QPoint(std::clamp(grab.x(), 0, std::max(0, size.width() - 1)),
                                std::clamp(grab.y(), 0, std::max(0, size.height() - 1))));

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
    std::function<QList<QTreeWidgetItem *>()> m_moving;
    std::function<QPixmap()> m_preview;
    std::function<QColor(QTreeWidgetItem *)> m_strip;
};

} // namespace

LayersPanel::LayersPanel(DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_blend(new QComboBox(this))
    , m_opacity(new QSlider(Qt::Horizontal, this))
    , m_opacityValue(new QSpinBox(this))
    , m_add(actionButton(IconId::Add, tr("New layer"), this))
    , m_merge(actionButton(IconId::MergeLayers, tr("Merge the selected layers"), this))
    , m_remove(actionButton(IconId::Delete, tr("Delete layer"), this))
    , m_camera(actionButton(IconId::Camera, tr("Camera: add it to the project, or select it"), this))
    , m_up(actionButton(IconId::ArrowUp, tr("Move up"), this))
    , m_down(actionButton(IconId::ArrowDown, tr("Move down"), this))
    , m_snapTo(actionButton(IconId::Crosshair,
                            tr("Snap to: make the selected perspective layer the one strokes snap to"), this))
    , m_maskAdd(actionButton(IconId::MaskAdd, tr("Create a mask layer: draw in black and white what the layer shows"),
                             this))
    , m_maskDelete(actionButton(IconId::MaskDelete, tr("Remove the mask layer: the layer shows whole again"), this))
{
    auto moving = [this] {
        QList<QTreeWidgetItem *> items;
        if (m_focus == Focus::Cel) {
            if (QTreeWidgetItem *item = celItem(m_document->currentCel()))
                items << item;
        } else if (m_focus == Focus::Layer) {
            for (doc::LayerId id : selectedLayers())
                if (QTreeWidgetItem *item = layerItem(id))
                    items << item;
        }
        return items;
    };
    m_tree = new LayerTree(
        moving, [this] { return dragPreview(); },
        [this](QTreeWidgetItem *item) {
            const doc::Document &d = m_document->document();
            const int index = kindOf(item) == Row::Camera || kindOf(item) == Row::Paper ? -1 : d.indexOf(celOf(item));
            if (index < 0)
                return QColor();
            const bool chosen = celOf(item) == m_document->currentCel() && !m_document->cameraSelected();
            return chosen ? TimelineView::colorForCel(index) : celRowColor(index, QApplication::palette());
        },
        this);

    for (doc::BlendMode mode : kBlendModes)
        m_blend->addItem(blendModeName(mode));
    m_opacity->setRange(0, 100);
    m_opacityValue->setRange(0, 100);
    m_opacityValue->setSuffix(QStringLiteral(" %"));
    m_opacityValue->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_opacityValue->setKeyboardTracking(false);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setIndentation(14);
    m_tree->setExpandsOnDoubleClick(false); // double-click renames
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Rows are dragged with Qt's drag and drop, but the drop is ours (see
    // eventFilter): layers and cels move in the document and the rows are rebuilt.
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setDropIndicatorShown(true);
    m_tree->viewport()->installEventFilter(this);

    auto *header = new QHBoxLayout;
    header->setSpacing(4);
    header->addWidget(m_blend);
    header->addWidget(m_opacity, 1);
    header->addWidget(m_opacityValue);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(0);
    buttons->addWidget(m_add);
    buttons->addWidget(m_merge);
    buttons->addWidget(m_remove);
    m_camera->setObjectName(QStringLiteral("cameraButton"));
    buttons->addWidget(m_camera);
    buttons->addStretch(1);
    buttons->addWidget(m_up);
    buttons->addWidget(m_down);

    // A row above the add and delete one: actions on the selected layer's kind (for now, snapping to a
    // perspective layer), from the right.
    m_snapTo->setObjectName(QStringLiteral("snapTo"));
    m_snapTo->setCheckable(true);
    m_snapTo->setEnabled(false);
    auto *layerActions = new QHBoxLayout;
    layerActions->setSpacing(0);
    layerActions->addStretch(1); // these start from the right
    m_maskAdd->setObjectName(QStringLiteral("maskAdd"));
    m_maskDelete->setObjectName(QStringLiteral("maskDelete"));
    m_maskAdd->setEnabled(false);
    m_maskDelete->setEnabled(false);
    layerActions->addWidget(m_maskAdd);
    layerActions->addWidget(m_maskDelete);
    layerActions->addWidget(m_snapTo);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addLayout(header);
    layout->addLayout(layerActions);
    layout->addLayout(buttons);
    layout->addWidget(m_tree, 1);

    // Header edits: of the current cel, or of the selected layers.
    connect(m_blend, &QComboBox::activated, this, [this](int index) {
        const doc::BlendMode blend = kBlendModes[std::size_t(index)];
        editSelectedLayers([blend](doc::LayerProperties &p) { p.blend = blend; });
    });
    // Opacity: while the slider is dragged the canvas follows it (a preview, nothing recorded);
    // on release the change is executed once, so a drag is a single undo step.
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        const QSignalBlocker block(m_opacityValue);
        m_opacityValue->setValue(v);
        if (m_opacity->isSliderDown())
            previewOpacity(v);
        else
            commitOpacity();
    });
    connect(m_opacity, &QSlider::sliderReleased, this, &LayersPanel::endOpacityPreview);
    connect(m_opacityValue, &QSpinBox::valueChanged, this, [this](int v) {
        const QSignalBlocker block(m_opacity);
        m_opacity->setValue(v);
        commitOpacity();
    });

    connect(m_add, &QToolButton::clicked, this, [this] { addClicked(doc::LayerKind::Raster); });
    // The arrow next to +: which kind of layer.
    m_addMenu = new QMenu(m_add);
    m_addMenu->addAction(tr("Layer"), this, [this] { addClicked(doc::LayerKind::Raster); });
    m_addMenu->addAction(tr("Perspective Layer"), this, [this] { addClicked(doc::LayerKind::Perspective); });
    connect(m_remove, &QToolButton::clicked, this, &LayersPanel::removeSelected);
    connect(m_merge, &QToolButton::clicked, this, &LayersPanel::mergeSelected);
    connect(m_snapTo, &QToolButton::clicked, this, &LayersPanel::snapToClicked);
    connect(m_maskAdd, &QToolButton::clicked, this, [this] { m_document->createLayerMask(); });
    connect(m_maskDelete, &QToolButton::clicked, this, [this] { m_document->deleteLayerMask(); });
    connect(m_document, &DocumentController::maskEditingChanged, this, &LayersPanel::updateMaskFrames);
    // The camera: made if the project has none, and selected.
    connect(m_camera, &QToolButton::clicked, this, [this] {
        m_focus = Focus::Camera;
        m_document->setLayerChosen(false);
        if (!m_document->camera())
            m_document->addCamera(); // the row is made as the change is told
        m_document->selectCamera(true);
        if (QTreeWidgetItem *row = cameraItem())
            m_tree->setCurrentItem(row);
        syncHeader();
    });
    // Right click on a row: the same commands, where the pointer is.
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested, this, &LayersPanel::showContextMenu);
    // Delete in the tree: the same as the bin.
    auto *remove = new QShortcut(m_tree);
    Shortcuts::instance().bind(remove, QStringLiteral("layers.delete"));
    connect(remove, &QShortcut::activated, this, [this] {
        if (m_remove->isEnabled())
            removeSelected();
    });
    remove->setContext(Qt::WidgetShortcut);
    connect(m_up, &QToolButton::clicked, this, [this] { moveSelected(+1); });
    connect(m_down, &QToolButton::clicked, this, [this] { moveSelected(-1); });

    // Rows chosen, selected, renamed.
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (item)
            rowChosen(item);
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        m_selected.clear();
        const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
        for (QTreeWidgetItem *item : selected)
            if (kindOf(item) == Row::Layer)
                m_selected << layerOf(item);
        // Ctrl+click took the current row out: another selected one takes its place.
        QTreeWidgetItem *current = m_tree->currentItem();
        if (current && !current->isSelected() && !selected.isEmpty()) {
            {
                const QSignalBlocker block(m_tree);
                m_tree->setCurrentItem(selected.last(), 0, QItemSelectionModel::NoUpdate);
            }
            rowChosen(selected.last());
        }
        syncHeader();
        paintRowColors();
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) { rename(item); });

    // Model -> view.
    m_thumbnailTimer.setSingleShot(true);
    m_thumbnailTimer.setInterval(250);
    connect(&m_thumbnailTimer, &QTimer::timeout, this, &LayersPanel::refreshThumbnails);
    m_fillTimer.setSingleShot(true);
    connect(&m_fillTimer, &QTimer::timeout, this, &LayersPanel::fillThumbnails);
    // Queued: a row's own button may trigger the change, and must not be
    // destroyed while its click is still being delivered.
    connect(
        m_document, &DocumentController::structureChanged, this,
        [this] {
            if (m_document->isPainting()) // the first stroke on an empty frame made the frame: the rows wait for the
                m_rebuildPending = true;  // pen to lift, a rebuild in the middle of a stroke would be felt
            else
                structureChangedNow();
        },
        Qt::QueuedConnection);
    connect(m_document, &DocumentController::historyChanged, this, [this] {
        if (m_rebuildPending && !m_document->isPainting())
            structureChangedNow();
    });
    connect(m_document, &DocumentController::documentReplaced, this, &LayersPanel::rebuildTree);
    // Another point of the timeline may show other frames.
    connect(m_document, &DocumentController::currentFrameChanged, this, [this] {
        markCurrentFrames();
        refreshSnapMarks();
        selectCurrentRow();
        if (!m_thumbnailTimer.isActive()) // the cels' thumbnails
            m_thumbnailTimer.start();
    });
    connect(m_document, &DocumentController::currentCelChanged, this, &LayersPanel::selectCurrentRow);
    connect(m_document, &DocumentController::editGuardChanged, this, &LayersPanel::refreshLockLabels);
    // The chosen cel's column is whole, the others' fainter: they follow the current cel.
    connect(m_document, &DocumentController::currentCelChanged, this, [this] { m_tree->viewport()->update(); });
    connect(m_document, &DocumentController::cameraSelectionChanged, this, [this] { m_tree->viewport()->update(); });
    // The camera's row follows the camera (a key, the eye, the camera added, taken away, undone).
    connect(m_document, &DocumentController::cameraChanged, this, [this] {
        if (cameraRowState(*m_document) != m_cameraRowState)
            rebuildTree();
    });
    // The camera went (or another row was chosen): back to a layer, if the panel still thought it selected.
    connect(m_document, &DocumentController::cameraSelectionChanged, this, [this] {
        if (m_document->cameraSelected() && m_focus != Focus::Camera) { // chosen elsewhere (the timeline)
            m_focus = Focus::Camera;
            m_document->setLayerChosen(false);
            selectCurrentRow();
            return;
        }
        if (!m_document->cameraSelected() && m_focus == Focus::Camera) {
            m_focus = Focus::Layer;
            m_document->setLayerChosen(true);
            selectCurrentRow();
        }
    });
    // Drawing on a cel's empty frame made it and selected its layer.
    connect(m_document, &DocumentController::layerChosenByDrawing, this, [this] {
        m_focus = Focus::Layer;
        selectCurrentRow();
    });
    connect(m_document, &DocumentController::currentLayerChanged, this, &LayersPanel::selectCurrentRow);
    connect(m_document, &DocumentController::pixelsChanged, this, [this](doc::CelId, doc::DrawingId drawing) {
        if (drawing != doc::kNoId)
            m_dirtyDrawings.insert(drawing); // only that frame's thumbnails need making again
        if (!m_thumbnailTimer.isActive())
            m_thumbnailTimer.start();
    });
    rebuildTree();
}

// --- rows ----------------------------------------------------------------------------

int LayersPanel::cameraRowState(const document::DocumentController &document)
{
    const auto &camera = document.camera();
    return camera ? int(camera->keys().size()) * 2 + (camera->visible ? 1 : 0) : -1;
}

QTreeWidgetItem *LayersPanel::cameraItem() const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        if (kindOf(m_tree->topLevelItem(i)) == Row::Camera)
            return m_tree->topLevelItem(i);
    return nullptr;
}

QTreeWidgetItem *LayersPanel::celItem(doc::CelId cel) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (kindOf(item) == Row::Cel && celOf(item) == cel)
            return item;
    }
    return nullptr;
}

QTreeWidgetItem *LayersPanel::frameItem(doc::DrawingId drawing) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *cel = m_tree->topLevelItem(i);
        for (int f = 0; f < cel->childCount(); ++f)
            if (drawingOf(cel->child(f)) == drawing)
                return cel->child(f);
    }
    return nullptr;
}

QTreeWidgetItem *LayersPanel::layerItem(doc::LayerId layer) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *cel = m_tree->topLevelItem(i);
        for (int f = 0; f < cel->childCount(); ++f) {
            QTreeWidgetItem *frame = cel->child(f);
            for (int k = 0; k < frame->childCount(); ++k)
                if (layerOf(frame->child(k)) == layer)
                    return frame->child(k);
        }
    }
    return nullptr;
}

void LayersPanel::rebuildTree()
{
    m_rebuildPending = false;
    m_dirtyDrawings.clear(); // the rows are made anew, and their thumbnails again
    m_pendingThumbnails.clear();
    const QSignalBlocker block(m_tree);
    // The folders as they were left: closed ones stay closed, new ones open.
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *cel = m_tree->topLevelItem(i);
        if (kindOf(cel) != Row::Cel)
            continue;
        auto remember = [](auto &closed, auto id, bool expanded) {
            if (expanded)
                closed.remove(id);
            else
                closed.insert(id);
        };
        remember(m_closedCels, celOf(cel), cel->isExpanded());
        for (int f = 0; f < cel->childCount(); ++f)
            if (const QTreeWidgetItem *frame = cel->child(f); frame->childCount() > 0) // else it cannot open
                remember(m_closedFrames, drawingOf(frame), frame->isExpanded());
    }
    const int scroll = m_tree->verticalScrollBar()->value();
    m_tree->clear();
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    const int frame = m_document->currentFrame();

    auto addRow = [this](QTreeWidgetItem *parentItem, Row kind, doc::CelId cel, doc::DrawingId drawing,
                         doc::LayerId layer, QWidget *row) {
        auto *item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(m_tree);
        item->setData(0, kKindRole, int(kind));
        item->setData(0, kCelRole, QVariant::fromValue(cel));
        item->setData(0, kDrawingRole, QVariant::fromValue(drawing));
        item->setData(0, kLayerRole, QVariant::fromValue(layer));
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDropEnabled;
        if (kind == Row::Cel || kind == Row::Layer)
            flags |= Qt::ItemIsDragEnabled;
        if (kind == Row::Paper)
            flags = Qt::ItemIsEnabled | Qt::ItemIsDropEnabled; // not selectable
        item->setFlags(flags);
        if (row) {
            item->setSizeHint(0, row->sizeHint());
            m_tree->setItemWidget(item, 0, row);
        }
        return item;
    };
    auto rowWidget = [this] {
        auto *row = new QWidget(m_tree);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(2, 2, 2, 2);
        h->setSpacing(4);
        return std::pair{row, h};
    };

    // The camera, above all the cels: an eye (is it on), its icon and how many keys it has.
    if (d.camera()) {
        auto [row, h] = rowWidget();
        auto *eye = toggleButton(IconId::Eye, tr("Turn the camera on / off: off, the picture is the whole sheet"),
                                 d.camera()->visible, row);
        h->addWidget(eye);
        h->addWidget(iconLabel(IconId::Camera, row));
        const int keys = int(d.camera()->keys().size());
        h->addWidget(new QLabel(tr("Camera\n%n keyframe(s)", "", keys), row), 1);
        connect(eye, &QToolButton::clicked, this, [this](bool on) { m_document->setCameraVisible(on); });
        QTreeWidgetItem *cameraRow = addRow(nullptr, Row::Camera, doc::kNoId, doc::kNoId, doc::kNoId, row);
        cameraRow->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); // not dragged, nothing dropped on it
    }
    m_cameraRowState = cameraRowState(*m_document);

    const QPixmap blank = thumbnail(nullptr, nullptr, docSize, nullptr);
    for (int i = d.celCount() - 1; i >= 0; --i) { // top cel first
        const doc::Cel &cel = d.celAt(i);
        const doc::CelId celId = cel.id();
        const doc::CelProperties &props = cel.properties();
        const doc::Drawing *shown = cel.drawingAt(frame);

        // The cel: a folder, with what it shows now.
        auto [row, h] = rowWidget();
        auto *eye = toggleButton(IconId::Eye, tr("Show / hide the cel"), props.visible, row);
        auto *lock = toggleButton(IconId::Lock, tr("Lock / unlock the cel"), props.locked, row);
        auto *thumb = new QLabel(row);
        thumb->setObjectName(QStringLiteral("thumbnail"));
        thumb->setPixmap(thumbnail(nullptr, shown, docSize, nullptr));
        h->addWidget(eye);
        h->addWidget(lock);
        h->addWidget(iconLabel(IconId::Folder, row));
        h->addWidget(thumb);
        auto *celText = new QLabel(propertiesText(props.opacity, props.blend, props.name), row);
        celText->setObjectName(QStringLiteral("rowText"));
        h->addWidget(celText, 1);
        auto toggleCel = [this, celId](auto change) {
            const doc::Cel *c = m_document->document().cel(celId);
            if (!c)
                return;
            doc::CelProperties p = c->properties();
            change(p);
            m_document->execute(doc::makeSetCelProperties(m_document->document(), celId, p));
        };
        connect(eye, &QToolButton::clicked, this,
                [toggleCel](bool visible) { toggleCel([visible](doc::CelProperties &p) { p.visible = visible; }); });
        connect(lock, &QToolButton::clicked, this,
                [toggleCel](bool locked) { toggleCel([locked](doc::CelProperties &p) { p.locked = locked; }); });
        QTreeWidgetItem *celRow = addRow(nullptr, Row::Cel, celId, doc::kNoId, doc::kNoId, row);
        celRow->setBackground(0, celRowColor(i, QApplication::palette())); // the cel's color, as in the timeline

        // Its frames, in the order the timeline first shows them (the ones
        // not shown anywhere last).
        std::vector<const doc::Drawing *> drawings;
        for (const auto &[id, drawing] : cel.drawings())
            drawings.push_back(&drawing);
        auto firstShown = [&cel](const doc::Drawing *drawing) {
            for (const doc::Exposure &e : cel.timing().exposures())
                if (e.drawing == drawing->id)
                    return e.start;
            return std::numeric_limits<int>::max();
        };
        std::stable_sort(drawings.begin(), drawings.end(),
                         [&](const doc::Drawing *a, const doc::Drawing *b) { return firstShown(a) < firstShown(b); });
        for (const doc::Drawing *drawing : drawings) {
            auto [frameRow, fh] = rowWidget();
            auto *frameThumb = new QLabel(frameRow);
            frameThumb->setObjectName(QStringLiteral("thumbnail"));
            frameThumb->setPixmap(blank); // made soon, apart (fillThumbnails())
            m_pendingThumbnails.push_back(drawing->id);
            auto *frameText = new QLabel(frameLabel(cel, *drawing, m_document->lockHolder(celId, drawing->id)), frameRow);
            frameText->setObjectName(QStringLiteral("frameText"));
            fh->addWidget(iconLabel(IconId::Frame, frameRow));
            fh->addWidget(frameThumb);
            fh->addWidget(frameText, 1);
            QTreeWidgetItem *frameItem = addRow(celRow, Row::Frame, celId, drawing->id, doc::kNoId, frameRow);

            // Its layers, top first.
            const int count = int(drawing->layers.size());
            for (int k = count - 1; k >= 0; --k) {
                const doc::Layer &layer = drawing->layers[std::size_t(k)];
                const doc::LayerId id = layer.id;
                const doc::DrawingId drawingId = drawing->id;
                auto [layerRow, lh] = rowWidget();
                auto *layerEye = toggleButton(IconId::Eye, tr("Show / hide"), layer.properties.visible, layerRow);
                auto *layerLock = toggleButton(IconId::Lock, tr("Lock / unlock"), layer.properties.locked, layerRow);
                auto *layerThumb = new ThumbLabel(layerRow);
                layerThumb->setObjectName(QStringLiteral("thumbnail"));
                layerThumb->setPixmap(blank);
                layerThumb->onPress = [this] { m_document->setMaskEditing(doc::kNoId); }; // draw on the layer
                lh->addWidget(layerEye);
                lh->addWidget(layerLock);
                lh->addWidget(layerThumb);
                if (layer.mask) { // its mask, at the right: the other thing to draw on
                    auto *maskThumb = new ThumbLabel(layerRow);
                    maskThumb->setObjectName(QStringLiteral("maskThumbnail"));
                    maskThumb->setPixmap(blank);
                    maskThumb->onPress = [this, id] { m_document->setMaskEditing(id); };
                    lh->addWidget(maskThumb);
                }
                auto *layerText = new QLabel(propertiesText(layer.properties.opacity, layer.properties.blend,
                                                            layer.properties.name),
                                             layerRow);
                layerText->setObjectName(QStringLiteral("rowText"));
                lh->addWidget(layerText, 1);
                if (layer.kind == doc::LayerKind::Perspective) {
                    // The sight of the layer strokes snap to (shown by refreshSnapMarks()).
                    auto *snapMark = iconLabel(IconId::Crosshair, layerRow);
                    snapMark->setObjectName(QStringLiteral("snapMark"));
                    snapMark->setToolTip(tr("Strokes snap to this layer"));
                    snapMark->setVisible(false);
                    lh->addWidget(snapMark);
                }
                auto toggle = [this, celId, drawingId, id](auto change) {
                    const doc::Layer *l = m_document->document().layer(celId, drawingId, id);
                    if (!l)
                        return;
                    doc::LayerProperties p = l->properties;
                    change(p);
                    m_document->execute(doc::makeSetLayerProperties(m_document->document(), celId, drawingId, id, p));
                };
                connect(layerEye, &QToolButton::clicked, this, [toggle](bool visible) {
                    toggle([visible](doc::LayerProperties &p) { p.visible = visible; });
                });
                connect(layerLock, &QToolButton::clicked, this,
                        [toggle](bool locked) { toggle([locked](doc::LayerProperties &p) { p.locked = locked; }); });
                addRow(frameItem, Row::Layer, celId, drawingId, id, layerRow);
            }
            frameItem->setExpanded(!m_closedFrames.contains(drawing->id));
        }
        celRow->setExpanded(!m_closedCels.contains(celId));
    }

    // The paper: always at the bottom, not a layer of the document, not selectable.
    auto [paperRow, ph] = rowWidget();
    const QColor paper = QColor::fromRgba(d.paperColor());
    auto *paperThumb = new QLabel(paperRow);
    paperThumb->setPixmap(thumbnail(nullptr, nullptr, docSize, &paper));
    ph->setContentsMargins(42, 2, 2, 2);
    ph->addWidget(paperThumb);
    ph->addWidget(new QLabel(tr("Paper"), paperRow), 1);
    addRow(nullptr, Row::Paper, doc::kNoId, doc::kNoId, doc::kNoId, paperRow);

    markCurrentFrames();
    refreshSnapMarks();
    if (!m_pendingThumbnails.empty())
        m_fillTimer.start(0); // the frames' and layers' thumbnails, a few at a time: a project of hundreds of big
                              // frames would otherwise hold the pen for seconds at every new frame
    m_shape = shapeKey();
    m_looks.clear();
    for (int i = 0; i < d.celCount(); ++i)
        for (const auto &[id, drawing] : d.celAt(i).drawings())
            m_looks[id] = looksOf(drawing);
    restoreSelection();
    paintRowColors();
    updateMaskFrames();
    m_tree->verticalScrollBar()->setValue(scroll);
    syncHeader();
}

void LayersPanel::paintRowColors()
{
    const QPalette app = QApplication::palette();
    // The selection is an outline (LayerTree::drawRow()), not a fill: the text and the glyphs of the rows keep their
    // colors, and the cel's own color shows whole.
    QPalette treePalette; // only what is set here is the tree's own; the rest follows the theme
    treePalette.setColor(QPalette::Highlight, Qt::transparent);
    m_tree->setPalette(treePalette);
    std::function<void(QTreeWidgetItem *)> paint = [&](QTreeWidgetItem *item) {
        if (kindOf(item) == Row::Cel) // the cel's color follows the theme, pale on a light one
            item->setBackground(0, celRowColor(m_document->document().indexOf(celOf(item)), app));
        if (QWidget *row = m_tree->itemWidget(item, 0)) {
            const QPalette pal = app;
            row->setPalette(pal);
            for (QLabel *label : row->findChildren<QLabel *>())
                label->setPalette(pal); // the style sheet gives each its own: the row's does not reach them
            for (QWidget *w : row->findChildren<QWidget *>()) {
                if (!w->property("glyph").isValid())
                    continue;
                auto *button = qobject_cast<QToolButton *>(w);
                showGlyph(w, !button || button->isChecked());
            }
        }
        for (int i = 0; i < item->childCount(); ++i)
            paint(item->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        paint(m_tree->topLevelItem(i));
}

void LayersPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && m_tree)
        paintRowColors(); // another theme: the colors set by hand are taken again
}

QString LayersPanel::shapeKey() const
{
    const doc::Document &d = m_document->document();
    QString key = QStringLiteral("%1x%2,%3,%4;").arg(d.width()).arg(d.height()).arg(d.paperColor()).arg(cameraRowState(*m_document));
    for (int i = 0; i < d.celCount(); ++i) {
        const doc::Cel &cel = d.celAt(i);
        key += QStringLiteral("c%1:").arg(cel.id());
        for (const auto &[id, drawing] : cel.drawings()) {
            key += QStringLiteral("d%1,%2,%3[").arg(id).arg(QString::fromStdString(drawing.name), exposureText(cel.timing(), id));
            for (const doc::Layer &layer : drawing.layers)
                key += QStringLiteral("%1k%2%3,").arg(layer.id).arg(int(layer.kind)).arg(layer.mask ? QLatin1Char('m') : QLatin1Char('-'));
            key += QLatin1Char(']');
        }
    }
    return key;
}

void LayersPanel::structureChangedNow()
{
    // Showing, locking or changing a cel or a layer leaves the rows as they are; making them all anew costs a
    // noticeable moment in a project of hundreds of frames.
    if (m_shape.isEmpty() || shapeKey() != m_shape) {
        rebuildTree();
        return;
    }
    updateLooks();
}

void LayersPanel::updateLooks()
{
    const doc::Document &d = m_document->document();
    auto setRow = [this](QTreeWidgetItem *item, bool visible, bool locked, const QString &text) {
        QWidget *row = item ? m_tree->itemWidget(item, 0) : nullptr;
        if (!row)
            return;
        const auto buttons = row->findChildren<QToolButton *>(QStringLiteral("LayerEye")); // the eye, then the lock
        if (buttons.size() >= 2) {
            buttons[0]->setChecked(visible);
            buttons[1]->setChecked(locked);
        }
        if (auto *label = row->findChild<QLabel *>(QStringLiteral("rowText")))
            label->setText(text);
    };
    bool thumbnails = false;
    for (int i = 0; i < d.celCount(); ++i) {
        const doc::Cel &cel = d.celAt(i);
        const doc::CelProperties &props = cel.properties();
        setRow(celItem(cel.id()), props.visible, props.locked, propertiesText(props.opacity, props.blend, props.name));
        for (const auto &[id, drawing] : cel.drawings()) {
            const QString looks = looksOf(drawing);
            QString &before = m_looks[id];
            if (looks == before)
                continue;
            before = looks;
            for (const doc::Layer &layer : drawing.layers) {
                const doc::LayerProperties &p = layer.properties;
                setRow(layerItem(layer.id), p.visible, p.locked, propertiesText(p.opacity, p.blend, p.name));
            }
            m_dirtyDrawings.insert(id); // its thumbnail shows only the visible layers
            thumbnails = true;
        }
    }
    if (thumbnails && !m_thumbnailTimer.isActive())
        m_thumbnailTimer.start();
    syncHeader();
}

void LayersPanel::updateMaskFrames()
{
    const doc::LayerId editing = m_document->maskEditingLayer();
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        if (kindOf(item) == Row::Layer) {
            if (QWidget *row = m_tree->itemWidget(item, 0)) {
                // Only ThumbLabels have these names.
                auto *maskThumb = static_cast<ThumbLabel *>(row->findChild<QLabel *>(QStringLiteral("maskThumbnail")));
                if (auto *layerThumb = static_cast<ThumbLabel *>(row->findChild<QLabel *>(QStringLiteral("thumbnail")))) {
                    layerThumb->setActive(maskThumb && layerOf(item) != editing); // outlined only where there is a choice
                    if (maskThumb)
                        maskThumb->setActive(layerOf(item) == editing);
                }
            }
        }
        for (int i = 0; i < item->childCount(); ++i)
            visit(item->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        visit(m_tree->topLevelItem(i));
}

void LayersPanel::fillThumbnails()
{
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    auto set = [this](QTreeWidgetItem *item, const QPixmap &pixmap) {
        if (QWidget *row = item ? m_tree->itemWidget(item, 0) : nullptr)
            if (auto *thumb = row->findChild<QLabel *>(QStringLiteral("thumbnail")))
                thumb->setPixmap(pixmap);
    };
    auto setLayer = [this, &set, docSize](QTreeWidgetItem *item, const doc::Layer &layer) { // and its mask's
        set(item, layerThumbnail(layer, docSize));
        if (QWidget *row = layer.mask && item ? m_tree->itemWidget(item, 0) : nullptr)
            if (auto *thumb = row->findChild<QLabel *>(QStringLiteral("maskThumbnail")))
                thumb->setPixmap(maskThumbnail(*layer.mask, docSize));
    };
    QElapsedTimer slice;
    slice.start();
    while (!m_pendingThumbnails.empty() && slice.elapsed() < 8) { // never long enough to be felt
        const doc::DrawingId id = m_pendingThumbnails.back(); // the last first: the frames nearest the top
        m_pendingThumbnails.pop_back();
        for (int i = 0; i < d.celCount(); ++i) {
            const doc::Drawing *drawing = d.celAt(i).drawing(id);
            if (!drawing)
                continue;
            set(frameItem(id), thumbnail(nullptr, drawing, docSize, nullptr));
            for (const doc::Layer &layer : drawing->layers)
                setLayer(layerItem(layer.id), layer);
            break;
        }
    }
    if (!m_pendingThumbnails.empty())
        m_fillTimer.start(0);
}

void LayersPanel::refreshThumbnails()
{
    const doc::Document &d = m_document->document();
    const QSize docSize(d.width(), d.height());
    const int frame = m_document->currentFrame();
    auto set = [this](QTreeWidgetItem *item, const QPixmap &pixmap) {
        if (QWidget *row = item ? m_tree->itemWidget(item, 0) : nullptr)
            if (auto *thumb = row->findChild<QLabel *>(QStringLiteral("thumbnail")))
                thumb->setPixmap(pixmap);
    };
    auto setLayer = [this, &set, docSize](QTreeWidgetItem *item, const doc::Layer &layer) { // and its mask's
        set(item, layerThumbnail(layer, docSize));
        if (QWidget *row = layer.mask && item ? m_tree->itemWidget(item, 0) : nullptr)
            if (auto *thumb = row->findChild<QLabel *>(QStringLiteral("maskThumbnail")))
                thumb->setPixmap(maskThumbnail(*layer.mask, docSize));
    };
    // The cels show the frame of the moment; a frame and its layers only change when painted on (rebuildTree()
    // makes all of them anew). A project of hundreds of big frames cannot afford more than that at every stroke.
    const QSet<doc::DrawingId> dirty = std::exchange(m_dirtyDrawings, {});
    for (int i = 0; i < d.celCount(); ++i) {
        const doc::Cel &cel = d.celAt(i);
        set(celItem(cel.id()), thumbnail(nullptr, cel.drawingAt(frame), docSize, nullptr));
        for (const doc::DrawingId id : dirty) {
            const doc::Drawing *drawing = cel.drawing(id);
            if (!drawing)
                continue;
            set(frameItem(id), thumbnail(nullptr, drawing, docSize, nullptr));
            for (const doc::Layer &layer : drawing->layers)
                setLayer(layerItem(layer.id), layer);
        }
    }
}

void LayersPanel::refreshLockLabels()
{
    const doc::Document &d = m_document->document();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *celRow = m_tree->topLevelItem(i);
        const doc::Cel *cel = kindOf(celRow) == Row::Cel ? d.cel(celOf(celRow)) : nullptr;
        for (int f = 0; cel && f < celRow->childCount(); ++f) {
            const doc::Drawing *drawing = cel->drawing(drawingOf(celRow->child(f)));
            QWidget *row = m_tree->itemWidget(celRow->child(f), 0);
            auto *text = row ? row->findChild<QLabel *>(QStringLiteral("frameText")) : nullptr;
            if (drawing && text)
                text->setText(frameLabel(*cel, *drawing, m_document->lockHolder(cel->id(), drawing->id)));
        }
    }
}

void LayersPanel::markCurrentFrames()
{
    const doc::Document &d = m_document->document();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *celRow = m_tree->topLevelItem(i);
        const doc::Cel *cel = kindOf(celRow) == Row::Cel ? d.cel(celOf(celRow)) : nullptr;
        const doc::DrawingId shown = cel ? cel->timing().drawingAt(m_document->currentFrame()) : doc::kNoId;
        for (int f = 0; f < celRow->childCount(); ++f) {
            QWidget *row = m_tree->itemWidget(celRow->child(f), 0);
            if (auto *text = row ? row->findChild<QLabel *>(QStringLiteral("frameText")) : nullptr) {
                QFont font = text->font();
                font.setBold(drawingOf(celRow->child(f)) == shown);
                text->setFont(font);
            }
        }
    }
}

// --- current row and selection ---------------------------------------------------

void LayersPanel::rowChosen(QTreeWidgetItem *item)
{
    const Row kind = kindOf(item);
    if (kind == Row::Paper)
        return;
    if (kind == Row::Camera) { // not a cel: nothing is current but the camera
        m_focus = Focus::Camera;
        m_document->setLayerChosen(false);
        m_document->selectCamera(true);
        syncHeader();
        return;
    }
    m_focus = kind == Row::Cel ? Focus::Cel : kind == Row::Layer ? Focus::Layer : Focus::Frame;
    m_document->selectCamera(false);
    m_document->setLayerChosen(m_focus == Focus::Layer);
    m_choosing = true;
    m_document->setCurrentCel(celOf(item));
    // A frame not shown here: to the first point it is shown at, if any.
    const doc::DrawingId drawing = drawingOf(item);
    const doc::Cel *cel = m_document->document().cel(celOf(item));
    if (cel && drawing != doc::kNoId && drawing != m_document->currentDrawingId())
        for (const doc::Exposure &e : cel->timing().exposures())
            if (e.drawing == drawing) {
                m_document->setCurrentFrame(e.start);
                break;
            }
    if (kind == Row::Layer)
        m_document->setCurrentLayer(layerOf(item));
    m_choosing = false;
    syncHeader();
}

// Makes `item` current without scrolling to it, which would open the
// folders it is in.
static void setCurrentQuietly(QTreeWidget *tree, QTreeWidgetItem *item, QItemSelectionModel::SelectionFlags flags)
{
    const bool autoScroll = tree->hasAutoScroll();
    tree->setAutoScroll(false);
    tree->setCurrentItem(item, 0, flags);
    tree->setAutoScroll(autoScroll);
}

// The row that stands for what is current, for the kind of row in focus.
static QTreeWidgetItem *currentRow(const LayersPanel &panel, LayersPanel::Focus focus,
                                   const DocumentController &document)
{
    if (focus == LayersPanel::Focus::Camera)
        if (QTreeWidgetItem *item = panel.cameraItem())
            return item;
    if (focus == LayersPanel::Focus::Layer)
        if (QTreeWidgetItem *item = panel.layerItem(document.currentLayer()))
            return item;
    if (focus != LayersPanel::Focus::Cel)
        if (QTreeWidgetItem *item = panel.frameItem(document.currentDrawingId()))
            return item;
    return panel.celItem(document.currentCel());
}

void LayersPanel::selectCurrentRow()
{
    if (m_choosing)
        return;
    // Chosen elsewhere (the timeline, a new layer...): that row alone selected.
    QTreeWidgetItem *target = currentRow(*this, m_focus, *m_document);
    if (target && target != m_tree->currentItem()) {
        const QSignalBlocker block(m_tree);
        setCurrentQuietly(m_tree, target, QItemSelectionModel::ClearAndSelect); // and only it selected
        m_selected.clear();
        if (kindOf(target) == Row::Layer)
            m_selected << layerOf(target);
    }
    syncHeader();
}

void LayersPanel::restoreSelection()
{
    // After a rebuild: the layers selected before that are still in the
    // current frame, and the current row (as current, without touching the rest).
    const QSignalBlocker block(m_tree);
    QList<doc::LayerId> kept;
    const doc::Drawing *drawing = m_document->currentDrawing();
    for (doc::LayerId id : m_selected)
        if (QTreeWidgetItem *item = drawing && drawing->layer(id) ? layerItem(id) : nullptr) {
            item->setSelected(true);
            kept << id;
        }
    if (QTreeWidgetItem *target = currentRow(*this, m_focus, *m_document)) {
        setCurrentQuietly(m_tree, target, QItemSelectionModel::NoUpdate);
        target->setSelected(true);
        if (kindOf(target) == Row::Layer && !kept.contains(layerOf(target)))
            kept << layerOf(target);
    }
    m_selected = kept;
}

QList<doc::LayerId> LayersPanel::selectedLayers() const
{
    QList<doc::LayerId> layers;
    const doc::Drawing *drawing = m_document->currentDrawing();
    if (!drawing)
        return layers;
    for (const doc::Layer &layer : drawing->layers)
        if (m_selected.contains(layer.id) || (m_focus == Focus::Layer && layer.id == m_document->currentLayer()))
            layers << layer.id;
    return layers;
}

QPixmap LayersPanel::dragPreview() const
{
    // The rows that move as shown, top first, stacked, on the selection color.
    QList<QTreeWidgetItem *> items;
    if (m_focus == Focus::Cel) {
        if (QTreeWidgetItem *item = celItem(m_document->currentCel()))
            items << item;
    } else {
        const QList<doc::LayerId> layers = selectedLayers();
        for (auto it = layers.rbegin(); it != layers.rend(); ++it) // top first
            if (QTreeWidgetItem *item = layerItem(*it))
                items << item;
    }
    QList<QPixmap> rows;
    int width = 0, height = 0;
    for (QTreeWidgetItem *item : items) {
        QWidget *row = m_tree->itemWidget(item, 0);
        if (!row)
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

QColor LayersPanel::celRowColor(int celIndex, const QPalette &palette)
{
    const QColor color = TimelineView::colorForCel(celIndex);
    if (palette.color(QPalette::Base).lightness() < 128)
        return color.darker(170); // on a dark theme, deep, under the light text
    // On a light one the color itself, a little lightened, under the dark text.
    const QColor base = palette.color(QPalette::Base);
    return QColor::fromRgbF(0.70 * color.redF() + 0.30 * base.redF(), 0.70 * color.greenF() + 0.30 * base.greenF(),
                            0.70 * color.blueF() + 0.30 * base.blueF());
}

// --- edits -----------------------------------------------------------------------------

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

void LayersPanel::previewOpacity(int percent)
{
    const float opacity = float(percent) / 100.0f;
    if (!m_opacityPreview) { // the drag begins: what it will go back to
        OpacityPreview start;
        if (const doc::Drawing *drawing = m_document->currentDrawing()) {
            for (doc::LayerId id : selectedLayers())
                start.layersBefore.emplace_back(id, drawing->layer(id)->properties);
        }
        m_opacityPreview = std::move(start);
    }
    const doc::Drawing *drawing = m_document->currentDrawing();
    if (!drawing)
        return;
    for (const auto &[id, before] : m_opacityPreview->layersBefore) {
        doc::LayerProperties shown = before;
        shown.opacity = opacity;
        m_document->previewLayerProperties({m_document->currentCel(), drawing->id, id}, shown);
    }
}

void LayersPanel::endOpacityPreview()
{
    if (m_opacityPreview) { // back to the start, so the command records the whole change
        const OpacityPreview start = std::move(*m_opacityPreview);
        m_opacityPreview.reset();
        if (const doc::Drawing *drawing = m_document->currentDrawing()) {
            for (const auto &[id, before] : start.layersBefore)
                m_document->previewLayerProperties({m_document->currentCel(), drawing->id, id}, before);
        }
    }
    commitOpacity();
}

void LayersPanel::commitOpacity()
{
    const float opacity = float(m_opacity->value()) / 100.0f;
    editSelectedLayers([opacity](doc::LayerProperties &p) { p.opacity = opacity; });
}

void LayersPanel::addClicked(doc::LayerKind kind)
{
    switch (m_focus) {
    case Focus::Cel: m_document->addCel(); break;
    case Focus::Frame:
        m_focus = Focus::Layer; // the new layer is shown as current
        m_document->setLayerChosen(true);
        m_document->addLayer(true, kind);
        break;
    case Focus::Layer: m_document->addLayer(false, kind); break;
    case Focus::Camera: break; // nothing is added from the camera
    }
}

void LayersPanel::removeSelected()
{
    if (m_focus == Focus::Camera) {
        m_document->removeCamera(); // the row goes with it (see the camera's changes)
        return;
    }
    if (m_focus == Focus::Cel) {
        if (m_document->document().celCount() > 1) // keep at least one cel
            m_document->execute(std::make_unique<doc::RemoveCelCommand>(m_document->currentCel()));
        return;
    }
    if (m_focus == Focus::Frame) {
        // Every selected frame, from whichever cel, with the points showing it.
        std::vector<std::unique_ptr<doc::Command>> steps;
        for (const QTreeWidgetItem *item : m_tree->selectedItems())
            if (kindOf(item) == Row::Frame)
                steps.push_back(std::make_unique<doc::RemoveDrawingCommand>(celOf(item), drawingOf(item)));
        if (!steps.empty())
            m_document->execute(
                std::make_unique<doc::GroupCommand>(doc::CommandKind::RemoveDrawing, std::move(steps)));
        return;
    }
    if (m_focus != Focus::Layer)
        return;
    std::vector<std::unique_ptr<doc::Command>> steps;
    for (const auto &[drawing, layers] : removableLayers())
        for (const LayerRow &row : layers)
            steps.push_back(std::make_unique<doc::RemoveLayerCommand>(row.cel, drawing.second, row.layer));
    if (steps.empty())
        return;
    m_selected.clear();
    m_document->execute(std::make_unique<doc::GroupCommand>(doc::CommandKind::RemoveLayer, std::move(steps)));
}

std::map<std::pair<doc::CelId, doc::DrawingId>, std::vector<LayersPanel::LayerRow>> LayersPanel::removableLayers() const
{
    // The selected layer rows, of whichever frames, grouped by frame: every
    // frame keeps at least one layer (its lowest chosen one stays when all go).
    std::map<std::pair<doc::CelId, doc::DrawingId>, std::vector<LayerRow>> chosen;
    for (const QTreeWidgetItem *item : m_tree->selectedItems())
        if (kindOf(item) == Row::Layer)
            chosen[{celOf(item), drawingOf(item)}].push_back({celOf(item), layerOf(item)});
    const doc::Document &d = m_document->document();
    for (auto it = chosen.begin(); it != chosen.end();) {
        const doc::Drawing *drawing = d.cel(it->first.first) ? d.cel(it->first.first)->drawing(it->first.second) : nullptr;
        auto &rows = it->second;
        if (!drawing) {
            it = chosen.erase(it);
            continue;
        }
        if (rows.size() >= drawing->layers.size()) {
            auto lowest = std::min_element(rows.begin(), rows.end(), [drawing](const LayerRow &a, const LayerRow &b) {
                return drawing->indexOf(a.layer) < drawing->indexOf(b.layer);
            });
            rows.erase(lowest);
        }
        it = rows.empty() ? chosen.erase(it) : std::next(it);
    }
    return chosen;
}

void LayersPanel::mergeSelected()
{
    if (m_focus != Focus::Layer)
        return;
    const QList<doc::LayerId> layers = selectedLayers();
    QString reason;
    m_selected.clear(); // the result is chosen when it becomes the current layer
    if (!m_document->mergeLayers(std::vector<doc::LayerId>(layers.begin(), layers.end()), &reason))
        emit statusMessage(reason);
}

void LayersPanel::copyLayers()
{
    if (copySelected())
        emit statusMessage(tr("Layers copied"));
}

bool LayersPanel::copySelected()
{
    std::vector<DocumentController::LayerRef> refs;
    for (const QTreeWidgetItem *item : m_tree->selectedItems())
        if (kindOf(item) == Row::Layer)
            refs.push_back({celOf(item), drawingOf(item), layerOf(item)});
    QString reason;
    if (!m_document->copyLayers(refs, &reason)) {
        emit statusMessage(reason);
        return false;
    }
    return true;
}

void LayersPanel::cutLayers()
{
    if (copySelected())
        removeSelected();
}

void LayersPanel::pasteLayers(bool merged)
{
    // Layers of the frame that are selected give way to what is pasted.
    QList<doc::LayerId> chosen;
    if (m_focus == Focus::Layer)
        chosen = selectedLayers();
    QString reason;
    if (!m_document->pasteLayers(merged, std::vector<doc::LayerId>(chosen.begin(), chosen.end()), &reason))
        emit statusMessage(reason);
}

bool LayersPanel::hasRowFocus() const
{
    return m_tree->hasFocus();
}

void LayersPanel::showContextMenu(const QPoint &at)
{
    QTreeWidgetItem *item = m_tree->itemAt(at);
    if (!item)
        return;
    if (!item->isSelected()) // the row under the pointer, unless it is one of the chosen
        m_tree->setCurrentItem(item);
    QMenu menu(this);
    // As the double click: the row under the pointer, whichever kind it is.
    menu.addAction(tr("Rename…"), this, [this, item] { rename(item); });
    menu.addSeparator();
    if (kindOf(item) == Row::Layer) {
        QAction *merge = menu.addAction(icon(IconId::MergeLayers), tr("Merge layers"), this, &LayersPanel::mergeSelected);
        merge->setEnabled(m_merge->isEnabled());
    }
    if (kindOf(item) == Row::Layer) {
        menu.addAction(tr("Cut layers"), this, &LayersPanel::cutLayers);
        menu.addAction(tr("Copy layers"), this, &LayersPanel::copyLayers);
    }
    if (kindOf(item) != Row::Cel) {
        // Pasted on top of the current frame: each layer on its own (the usual), or as one.
        QAction *separate = menu.addAction(tr("Paste layers"), this, [this] { pasteLayers(false); });
        QAction *merged = menu.addAction(tr("Paste layers merged"), this, [this] { pasteLayers(true); });
        separate->setEnabled(m_document->hasCopiedLayers());
        merged->setEnabled(m_document->hasCopiedLayers());
    }
    menu.addSeparator();
    QAction *remove = menu.addAction(icon(IconId::Delete), m_remove->toolTip(), this, &LayersPanel::removeSelected);
    remove->setEnabled(m_remove->isEnabled());
    menu.exec(m_tree->viewport()->mapToGlobal(at));
}

void LayersPanel::moveCurrentCel(int index)
{
    const doc::Document &d = m_document->document();
    const doc::CelId cel = m_document->currentCel();
    index = std::clamp(index, 0, d.celCount() - 1);
    if (d.cel(cel) && index != d.indexOf(cel))
        m_document->execute(std::make_unique<doc::MoveCelCommand>(cel, index));
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
    if (m_focus == Focus::Cel) {
        moveCurrentCel(m_document->document().indexOf(m_document->currentCel()) + direction);
        return;
    }
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

void LayersPanel::rename(QTreeWidgetItem *item)
{
    const doc::Document &d = m_document->document();
    const Row kind = kindOf(item);
    if (kind == Row::Cel) {
        const doc::CelId id = celOf(item);
        const doc::Cel *cel = d.cel(id);
        bool ok = false;
        const QString name = cel ? QInputDialog::getText(this, tr("Rename cel"), tr("Name:"), QLineEdit::Normal,
                                                         QString::fromStdString(cel->properties().name), &ok)
                                 : QString();
        cel = m_document->document().cel(id);
        if (!ok || name.trimmed().isEmpty() || !cel)
            return;
        doc::CelProperties props = cel->properties();
        props.name = name.trimmed().toStdString();
        m_document->execute(doc::makeSetCelProperties(d, id, props));
    } else if (kind == Row::Layer) {
        const doc::CelId cel = celOf(item);
        const doc::LayerId id = layerOf(item);
        const doc::DrawingId drawing = d.cel(cel) ? d.cel(cel)->timing().drawingAt(m_document->currentFrame())
                                                  : doc::kNoId;
        const doc::Layer *layer = d.layer(cel, drawing, id);
        bool ok = false;
        const QString name = layer ? QInputDialog::getText(this, tr("Rename layer"), tr("Name:"), QLineEdit::Normal,
                                                           QString::fromStdString(layer->properties.name), &ok)
                                   : QString();
        layer = m_document->document().layer(cel, drawing, id);
        if (!ok || name.trimmed().isEmpty() || !layer)
            return;
        doc::LayerProperties props = layer->properties;
        props.name = name.trimmed().toStdString();
        m_document->execute(doc::makeSetLayerProperties(d, cel, drawing, id, props));
    }
}

// --- drops ------------------------------------------------------------------------------

bool LayersPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree->viewport() && event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        if (drop->source() == m_tree) {
            // Ignored for Qt, so the view itself neither moves nor removes rows.
            drop->setDropAction(Qt::IgnoreAction);
            drop->accept();
            dropAt(drop->position().toPoint());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool LayersPanel::dropAt(const QPoint &pos)
{
    QTreeWidgetItem *target = m_tree->itemAt(pos);
    if (!target)
        return false;
    const bool lowerHalf = pos.y() > m_tree->visualItemRect(target).center().y();
    const Row kind = kindOf(target);

    if (m_focus == Focus::Layer) {
        // Within their own frame: before the layer under the pen (after it in
        // its lower half), or on top when dropped on the frame's row.
        if (kind == Row::Paper || drawingOf(target) != m_document->currentDrawingId())
            return false;
        if (kind == Row::Frame) {
            moveSelectedToRow(0);
            return true;
        }
        if (kind != Row::Layer)
            return false;
        moveSelectedToRow(target->parent()->indexOfChild(target) + (lowerHalf ? 1 : 0));
        return true;
    }
    if (m_focus != Focus::Cel)
        return false;

    // A cel among the cels: before the cel under the pen (after it in the
    // lower half of its row, or anywhere in its folder), to the bottom on the paper.
    const doc::Document &d = m_document->document();
    const int n = d.celCount();
    int row = n; // rows top first
    if (kind != Row::Paper) {
        QTreeWidgetItem *top = target;
        while (top->parent())
            top = top->parent();
        row = m_tree->indexOfTopLevelItem(top) + (lowerHalf || top != target ? 1 : 0);
    }
    const int from = n - 1 - d.indexOf(m_document->currentCel()); // its row
    const int to = row > from ? row - 1 : row;                     // counted without it
    moveCurrentCel(n - 1 - std::clamp(to, 0, n - 1));
    return true;
}

// --- snapping ------------------------------------------------------------------------------

void LayersPanel::setToolState(tools::ToolState *state)
{
    m_toolState = state;
    if (!state)
        return;
    connect(state, &tools::ToolState::perspectiveSnapChanged, this, [this] {
        refreshSnapMarks();
        syncHeader();
    });
    refreshSnapMarks();
    syncHeader();
}

bool LayersPanel::isSnapReference(doc::LayerId layer) const
{
    // One layer at most, exactly the one chosen.
    return m_toolState && m_toolState->perspectiveSnapLayer() != doc::kNoId
           && layer == m_toolState->perspectiveSnapLayer();
}

void LayersPanel::refreshSnapMarks()
{
    std::function<void(QTreeWidgetItem *)> visit = [&](QTreeWidgetItem *item) {
        if (kindOf(item) == Row::Layer)
            if (QWidget *row = m_tree->itemWidget(item, 0))
                if (auto *mark = row->findChild<QLabel *>(QStringLiteral("snapMark")))
                    mark->setVisible(isSnapReference(layerOf(item)));
        for (int i = 0; i < item->childCount(); ++i)
            visit(item->child(i));
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        visit(m_tree->topLevelItem(i));
}

void LayersPanel::snapToClicked(bool on)
{
    const doc::Layer *layer = m_document->currentDrawing() ? m_document->currentDrawing()->layer(m_document->currentLayer())
                                                          : nullptr;
    if (!m_toolState || !layer || layer->kind != doc::LayerKind::Perspective)
        return;
    // On a layer not snapped to: it is, and any other is not any more. On the one snapped to: none.
    m_toolState->setPerspectiveSnapLayer(on ? layer->id : doc::kNoId);
}

// --- header ------------------------------------------------------------------------------

void LayersPanel::publishSelection()
{
    const doc::Document &d = m_document->document();
    std::vector<DocumentController::LayerRef> layers;
    auto add = [&layers](doc::CelId cel, const doc::Drawing &drawing, doc::LayerId only) {
        for (const doc::Layer &layer : drawing.layers) {
            const DocumentController::LayerRef ref{cel, drawing.id, layer.id};
            const bool wanted = only == doc::kNoId || layer.id == only;
            if (wanted && std::find(layers.begin(), layers.end(), ref) == layers.end())
                layers.push_back(ref);
        }
    };
    for (const QTreeWidgetItem *item : m_tree->selectedItems()) {
        const doc::Cel *cel = d.cel(celOf(item));
        if (!cel)
            continue;
        if (kindOf(item) == Row::Cel) {
            for (const auto &[id, drawing] : cel->drawings())
                add(cel->id(), drawing, doc::kNoId);
        } else if (const doc::Drawing *drawing = cel->drawing(drawingOf(item))) {
            add(cel->id(), *drawing, kindOf(item) == Row::Layer ? layerOf(item) : doc::kNoId);
        }
    }
    m_document->setLayerSelection(std::move(layers));
}

void LayersPanel::syncHeader()
{
    publishSelection();
    const doc::Document &d = m_document->document();
    const doc::Cel *cel = d.cel(m_document->currentCel());
    // A frame's or a layer's row stands for the frame shown now, or for none.
    const QTreeWidgetItem *row = m_tree->currentItem();
    const bool shownNow = row && drawingOf(row) == m_document->currentDrawingId();
    const doc::Drawing *drawing = m_focus == Focus::Cel || shownNow ? m_document->currentDrawing() : nullptr;
    const doc::Layer *layer = drawing ? drawing->layer(m_document->currentLayer()) : nullptr;

    bool header = false, canAdd = false, canRemove = false, canUp = false, canDown = false, canMerge = false;
    float opacity = 1.0f;
    doc::BlendMode blend = doc::BlendMode::Normal;
    switch (m_focus) {
    case Focus::Cel: {
        const int index = cel ? d.indexOf(cel->id()) : -1;
        canAdd = cel != nullptr; // its blend mode and opacity are the timeline's to change
        canRemove = cel && d.celCount() > 1; // keep at least one cel
        canUp = cel && index + 1 < d.celCount();
        canDown = cel && index > 0;
        if (cel) {
            opacity = cel->properties().opacity;
            blend = cel->properties().blend;
        }
        m_add->setToolTip(tr("New cel"));
        m_remove->setToolTip(tr("Delete the cel"));
        m_up->setToolTip(tr("Move the cel up"));
        m_down->setToolTip(tr("Move the cel down"));
        break;
    }
    case Focus::Camera:
        canRemove = m_document->camera().has_value();
        m_add->setToolTip(tr("New cel"));
        m_remove->setToolTip(tr("Delete the camera"));
        m_up->setToolTip(tr("Move up"));
        m_down->setToolTip(tr("Move down"));
        break;
    case Focus::Frame:
        canAdd = drawing != nullptr;
        for (const QTreeWidgetItem *item : m_tree->selectedItems())
            canRemove = canRemove || kindOf(item) == Row::Frame;
        m_add->setToolTip(tr("New layer on top of the frame"));
        m_remove->setToolTip(tr("Delete the selected frames"));
        m_up->setToolTip(tr("Move up"));
        m_down->setToolTip(tr("Move down"));
        break;
    case Focus::Layer: {
        // Up and down while some selected layer can still go that way.
        const QList<doc::LayerId> selected = drawing ? selectedLayers() : QList<doc::LayerId>();
        const int n = drawing ? int(drawing->layers.size()) : 0;
        for (doc::LayerId id : selected) {
            const int i = drawing->indexOf(id);
            canUp = canUp || (i + 1 < n && !selected.contains(drawing->layers[std::size_t(i + 1)].id));
            canDown = canDown || (i > 0 && !selected.contains(drawing->layers[std::size_t(i - 1)].id));
        }
        header = layer != nullptr;
        canAdd = drawing != nullptr;
        canRemove = !removableLayers().empty();
        canMerge = drawing
                   && m_document->canMergeLayers(std::vector<doc::LayerId>(selected.begin(), selected.end()));
        canUp = layer && canUp;
        canDown = layer && canDown;
        if (layer) {
            opacity = layer->properties.opacity;
            blend = layer->properties.blend;
        }
        m_add->setToolTip(tr("New layer"));
        m_remove->setToolTip(tr("Delete the selected layers"));
        m_up->setToolTip(tr("Move the selected layers up"));
        m_down->setToolTip(tr("Move the selected layers down"));
        break;
    }
    }
    for (QWidget *w : std::initializer_list<QWidget *>{m_blend, m_opacity, m_opacityValue})
        w->setEnabled(header);
    m_add->setEnabled(canAdd);
    // The kinds of layer to add, unless + adds a cel.
    m_add->setMenu(m_focus == Focus::Cel ? nullptr : m_addMenu);
    m_add->setPopupMode(m_focus == Focus::Cel ? QToolButton::DelayedPopup : QToolButton::MenuButtonPopup);
    m_remove->setEnabled(canRemove);
    m_merge->setEnabled(canMerge);
    m_up->setEnabled(canUp);
    m_down->setEnabled(canDown);
    // Snap to: on a perspective layer; pressed while strokes snap to it.
    const bool snappable = m_toolState && m_focus == Focus::Layer && layer && layer->kind == doc::LayerKind::Perspective;
    {
        const QSignalBlocker block(m_snapTo);
        m_snapTo->setEnabled(snappable);
        m_snapTo->setChecked(snappable && isSnapReference(layer->id));
    }
    // The mask: made for a raster layer that has none, taken away from one that has; one layer at a time.
    const bool maskable = m_focus == Focus::Layer && layer && layer->kind == doc::LayerKind::Raster
                          && selectedLayers().size() <= 1 && drawing;
    m_maskAdd->setEnabled(maskable && !layer->mask);
    m_maskDelete->setEnabled(maskable && layer->mask);
    if (!header && m_focus != Focus::Cel)
        return;

    const QSignalBlocker b1(m_blend), b2(m_opacity), b3(m_opacityValue);
    m_blend->setCurrentIndex(blendModeIndex(blend));
    const int value = int(std::lround(opacity * 100));
    m_opacity->setValue(value);
    m_opacityValue->setValue(value);
}

} // namespace mothion::ui
