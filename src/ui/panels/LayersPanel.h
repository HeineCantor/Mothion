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
// The header edits the blend mode and opacity of the selected layers,
// delete and the arrows act on all of them; each row toggles visibility
// and lock; double-click renames. Every edit is one undoable command.
class LayersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(document::DocumentController *document, QWidget *parent = nullptr);

    // The selected layers, bottom first; at least the current one.
    QList<doc::LayerId> selectedLayers() const;

private:
    void rebuildList();      // structure changed: recreate the rows
    void syncHeader();       // current layer changed: blend / opacity / buttons
    void refreshThumbnails(); // throttled: pixels or frame changed
    void selectCurrentRow();
    void restoreSelection(); // after rebuildList()
    void renameLayer(doc::LayerId id);
    void commitOpacity();
    void removeSelected();
    void moveSelected(int direction); // +1 up, -1 down, each by one place

    template<typename Edit>
    void editSelectedLayers(Edit edit); // runs `edit` on copies of the properties, then executes

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
    QList<doc::LayerId> m_selected; // kept across rebuilds of the rows
};

} // namespace mothion::ui
