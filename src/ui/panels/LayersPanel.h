#pragma once

#include "core/document/Ids.h"

#include <QList>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QListWidget;
class QSlider;
class QSpinBox;
class QToolButton;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// The layer stack of the document, top layer first, with the paper at the
// bottom. Selecting a row makes it the current layer; Ctrl+click adds or
// takes away a layer, Shift+click selects a range, as in a file manager.
// Dragging rows moves the selected layers to where they are dropped.
// The header edits the blend mode and opacity of the selected layers,
// delete and the arrows act on all of them; each row toggles visibility
// and lock; double-click renames. Every edit is one undoable command.
class LayersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(document::DocumentController *document, QWidget *parent = nullptr);

    // The selected layers, bottom first; at least the current one.
    QList<doc::CelId> selectedCels() const;
    // Moves the selected layers, in their order, before list row `row`
    // (rows are top first; the paper's row = to the bottom). One undo step.
    void moveSelectedToRow(int row);
    // What follows the pen while rows are dragged: the selected rows as they
    // look, a little transparent.
    QPixmap dragPreview() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildList();      // structure changed: recreate the rows
    void syncHeader();       // current layer changed: blend / opacity / buttons
    void refreshThumbnails(); // throttled: pixels or frame changed
    void selectCurrentRow();
    void restoreSelection(); // after rebuildList()
    void renameCel(doc::CelId id);
    void commitOpacity();
    void removeSelected();
    void moveSelected(int direction); // +1 up, -1 down, each by one place

    template<typename Edit>
    void editSelectedCels(Edit edit); // runs `edit` on copies of the properties, then executes

    document::DocumentController *m_document;
    QComboBox *m_blend;
    QSlider *m_opacity;
    QSpinBox *m_opacityValue;
    QListWidget *m_list;
    QToolButton *m_add;
    QToolButton *m_remove;
    QToolButton *m_up;
    QToolButton *m_down;
    QTimer m_thumbnailTimer;
    QList<doc::CelId> m_selected; // kept across rebuilds of the rows
};

} // namespace mothion::ui
