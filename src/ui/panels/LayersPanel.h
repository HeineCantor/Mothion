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

    enum class Focus { Cel, Frame, Layer };
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    // A command could not be done; the text says why.
    void statusMessage(const QString &text);

private:
    void rebuildTree();       // structure changed: recreate the rows
    void syncHeader();        // current row changed: blend / opacity / buttons, publishSelection()
    void publishSelection();  // the chosen rows' layers, for the tools (a move)
    void refreshThumbnails(); // throttled: pixels or the current frame changed
    void markCurrentFrames(); // the frames shown now, in bold
    void selectCurrentRow();  // the current cel / layer chosen elsewhere
    void restoreSelection();  // after rebuildTree()
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
    bool isSnapReference(doc::LayerId layer) const;
    void moveSelected(int direction); // +1 up, -1 down, each by one place
    void moveCurrentCel(int index);
    bool dropAt(const QPoint &pos);   // a drop from the tree itself

    template<typename Edit>
    void editSelectedLayers(Edit edit); // runs `edit` on copies of the properties, then executes
    template<typename Edit>
    void editCurrentCel(Edit edit);

    document::DocumentController *m_document;
    QComboBox *m_blend;
    QSlider *m_opacity;
    QSpinBox *m_opacityValue;
    QTreeWidget *m_tree;
    QToolButton *m_add;
    QMenu *m_addMenu = nullptr; // the kinds of layer, from the arrow next to +
    QToolButton *m_merge;
    QToolButton *m_remove;
    QToolButton *m_up;
    QToolButton *m_down;
    QToolButton *m_snapTo;  // the layer actions' second row
    tools::ToolState *m_toolState = nullptr;
    QTimer m_thumbnailTimer;
    Focus m_focus = Focus::Layer;
    struct OpacityPreview
    {
        bool cel = false;
        doc::CelId celId = doc::kNoId;
        doc::CelProperties celBefore;
        std::vector<std::pair<doc::LayerId, doc::LayerProperties>> layersBefore;
    };
    std::optional<OpacityPreview> m_opacityPreview; // while the slider is dragged
    bool m_choosing = false;        // a row click is making its cel and layer current
    QList<doc::LayerId> m_selected; // kept across rebuilds of the rows
    QSet<doc::CelId> m_closedCels;       // folders closed by hand
    QSet<doc::DrawingId> m_closedFrames;
};

} // namespace mothion::ui
