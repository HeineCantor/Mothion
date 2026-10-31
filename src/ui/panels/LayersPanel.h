#pragma once

#include "core/document/Ids.h"

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
// bottom. Selecting a row makes it the current layer. The header edits the
// current layer's blend mode and opacity; each row toggles visibility and
// lock; double-click renames. Every edit is one undoable command.
class LayersPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LayersPanel(document::DocumentController *document, QWidget *parent = nullptr);

private:
    void rebuildList();      // structure changed: recreate the rows
    void syncHeader();       // current layer changed: blend / opacity / buttons
    void refreshThumbnails(); // throttled: pixels or frame changed
    void selectCurrentRow();
    void renameLayer(doc::LayerId id);
    void commitOpacity();

    template<typename Edit>
    void editCurrentLayer(Edit edit); // runs `edit` on a copy of the properties, then executes

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
};

} // namespace mothion::ui
