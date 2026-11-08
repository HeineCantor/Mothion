#pragma once

#include "core/document/Cel.h"
#include "core/document/Drawing.h"
#include "core/document/Ids.h"

#include <QList>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <map>
#include <optional>
#include <utility>
#include <vector>

class QComboBox;
class QMenu;
class QSlider;
class QSpinBox;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::ui {

// The document as folders: each cel (top cel first) holds all its frames,
// in the order the timeline first shows them, and each frame holds its
// layers (top layer first); the paper is at the bottom. The frame shown at
// the current point of the timeline is in bold. Cels and frames open and
// close like folders. Choosing a frame or one of its layers goes to the
// first point the frame is shown at, so that what is edited is on the canvas.
//
// The current row says what the header and the buttons act on:
//  - the camera (a row at the top, with the Camera button): drawing is refused, the canvas moves the camera
//    with the pen; its eye turns it on and off; delete removes it;
//  - a cel: its blend mode and opacity; new cel, delete, up and down;
//  - a frame: new layer on top of it (the arrow next to +: a layer or a
//    perspective layer); delete removes every selected frame
//    (Ctrl and Shift select several, of any cel);
//  - a layer: the blend mode and opacity of the selected layers (of that
//    frame: Ctrl+click adds or takes away one, Shift+click a range); new
//    layer above (of either kind), merge (into the lowest), delete, up and
//    down; right click on a row offers merge and delete; "Snap to" (on a perspective layer) makes
//    it the reference of the pen's snapping. A perspective
//    layer is set on the canvas (canvas::PerspectiveGizmo).
// The tools that act on several layers at once (the move) take every layer
// of the selected rows: a frame stands for its layers, a cel for all the
// layers of its frames.
// Dragging rows moves the selected layers within their frame, or a cel
// among the cels. Each cel and layer row toggles visibility and lock;
// double-click renames. Every edit is one undoable command.
class LayersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(document::DocumentController *document, QWidget *parent = nullptr);

    // The background of a cel's row: the color of its blocks in the timeline, deep on a dark theme (under light
    // text) and the color itself, a little lightened, on a light one (under dark text).
    static QColor celRowColor(int celIndex, const QPalette &palette);

    // The layer clipboard (Ctrl+C and Ctrl+V while the rows have the focus, and the
    // right click menu): copies the selected layers, of whichever frames; pastes them on
    // top of the current frame, as layers of their own or, `merged`, as one.
    // Cut: the same, and the layers go. Pasting with layers of the frame selected puts the
    // pasted ones in their place (see DocumentController::pasteLayers).
    void copyLayers();
    void cutLayers();
    void pasteLayers(bool merged);
    // The tool state, for the snap reference: "Snap to" makes the selected perspective layer the one
    // strokes snap to (shown by a sight on its row).
    void setToolState(tools::ToolState *state);
    // True when a row of the panel has the keyboard focus.
    bool hasRowFocus() const;

    enum class Focus { Cel, Frame, Layer, Camera };
    Focus focus() const { return m_focus; }

    // The selected layers of the current frame, bottom first; at least the
    // current one when a layer row is current.
    QList<doc::LayerId> selectedLayers() const;
    // Moves the selected layers, in their order, before layer row `row` of
    // their frame (rows are top first; the number of layers = to the bottom).
    // One undo step.
    void moveSelectedToRow(int row);
    // What follows the pen while rows are dragged: the rows that move as
    // they look, a little transparent.
    QPixmap dragPreview() const;

    // The rows: a cel's, a frame's (a drawing's), a layer's.
    QTreeWidgetItem *celItem(doc::CelId cel) const;
    QTreeWidgetItem *frameItem(doc::DrawingId drawing) const;
    QTreeWidgetItem *layerItem(doc::LayerId layer) const;
    QTreeWidgetItem *cameraItem() const; // the camera's row, at the top (none without a camera)

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

signals:
    // A command could not be done; the text says why.
    void statusMessage(const QString &text);

private:
    void rebuildTree();       // structure changed: recreate the rows
    // The structure changed: the rows are made anew, or, when only how cels and layers look changed (shown, locked,
    // opacity, blend, name), they are updated where they are.
    void structureChangedNow();
    void updateLooks();
    QString shapeKey() const; // what the rows are made of, not how they look
    void syncHeader();        // current row changed: blend / opacity / buttons, publishSelection()
    void publishSelection();  // the chosen rows' layers, for the tools (a move)
    void fillThumbnails();    // the pending ones, for a few milliseconds, then again from the event loop
    void refreshThumbnails(); // throttled: the cels' (the frame changed) and those of the frames painted on
    void markCurrentFrames();
    void refreshLockLabels(); // the frames somebody else is working on say who (a shared session) // the frames shown now, in bold
    void selectCurrentRow();  // the current cel / layer chosen elsewhere
    void restoreSelection();  // after rebuildTree()
    void paintRowColors();    // the text and glyphs of the selected rows, in the highlighted text color
    void rowChosen(QTreeWidgetItem *item);
    void rename(QTreeWidgetItem *item);
    void commitOpacity();
    // While the opacity slider is dragged the canvas follows it: the cel or the
    // selected layers show the value at once, and on release they go back to
    // what they were and the change is executed as one undo step.
    void previewOpacity(int percent);
    void endOpacityPreview();
    void addClicked(doc::LayerKind kind);
    void removeSelected();
    struct LayerRow
    {
        doc::CelId cel;
        doc::LayerId layer;
    };
    // The layers Delete would take, by (cel, frame): all the selected layer rows.
    std::map<std::pair<doc::CelId, doc::DrawingId>, std::vector<LayerRow>> removableLayers() const;
    void mergeSelected();
    bool copySelected(); // to the layer clipboard; false (and a message) if none
    void showContextMenu(const QPoint &at);
    void snapToClicked(bool on);
    // Shows the sight on the perspective layer(s) strokes snap to.
    void refreshSnapMarks();
    void updateMaskFrames(); // which of a layer's thumbnails, itself or its mask, is outlined: the one drawn on
    bool isSnapReference(doc::LayerId layer) const;
    void moveSelected(int direction); // +1 up, -1 down, each by one place
    void moveCurrentCel(int index);
    bool dropAt(const QPoint &pos);   // a drop from the tree itself

    template<typename Edit>
    void editSelectedLayers(Edit edit); // runs `edit` on copies of the properties, then executes

    document::DocumentController *m_document;
    QComboBox *m_blend;
    QSlider *m_opacity;
    QSpinBox *m_opacityValue;
    QTreeWidget *m_tree;
    QToolButton *m_add;
    QMenu *m_addMenu = nullptr; // the kinds of layer, from the arrow next to +
    QToolButton *m_merge;
    QToolButton *m_remove;
    QToolButton *m_camera; // adds the camera, or selects it
    QToolButton *m_up;
    QToolButton *m_down;
    QToolButton *m_snapTo;  // the layer actions' second row
    QToolButton *m_maskAdd;    // make / take away the current layer's mask (left of the snap button)
    QToolButton *m_maskDelete;
    tools::ToolState *m_toolState = nullptr;
    QTimer m_thumbnailTimer;
    QTimer m_fillTimer;
    QString m_shape;                                // shapeKey() of the rows now
    std::map<doc::DrawingId, QString> m_looks;      // how each frame's layers looked when their rows were set
    bool m_rebuildPending = false; // the structure changed during a stroke: the rows are made when it ends
    std::vector<doc::DrawingId> m_pendingThumbnails; // frames whose thumbnails (and their layers') are still to make
    QSet<doc::DrawingId> m_dirtyDrawings; // frames whose pixels changed since their thumbnails were made
    // What the camera's row shows (-1: no camera; else twice the number of keys, plus one if it is on): the rows are
    // made again only when it changes, not at each step of a drag.
    int m_cameraRowState = -2;
    static int cameraRowState(const document::DocumentController &document);
    Focus m_focus = Focus::Layer;
    struct OpacityPreview
    {
        std::vector<std::pair<doc::LayerId, doc::LayerProperties>> layersBefore;
    };
    std::optional<OpacityPreview> m_opacityPreview; // while the slider is dragged
    bool m_choosing = false;        // a row click is making its cel and layer current
    QList<doc::LayerId> m_selected; // kept across rebuilds of the rows
    QSet<doc::CelId> m_closedCels;       // folders closed by hand
    QSet<doc::DrawingId> m_closedFrames;
};

} // namespace mothion::ui
