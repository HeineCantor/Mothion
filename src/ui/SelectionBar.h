#pragma once

#include <QFrame>

namespace mothion::canvas {
class CanvasView;
}
namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::ui {

// Commands for the selection, floating over the canvas a few pixels under
// it (above it when there is no room below), shown only while something is
// selected: deselect, invert, expand, shrink, erase inside or outside, cut and copy to a new layer, transform,
// fill, flip.
class SelectionBar : public QFrame
{
    Q_OBJECT

public:
    SelectionBar(canvas::CanvasView *canvas, document::DocumentController *document, tools::ToolState *toolState);

signals:
    // A command could not be done; the text says why.
    void statusMessage(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();
    // A command of the document that may fail, run; why it did not, in the status bar.
    void runWithReason(bool (document::DocumentController::*command)(QString *));

    // Asks how many pixels, then expands (`out`) or shrinks the selection by that much.
    void grow(bool out);

    canvas::CanvasView *m_canvas;
    document::DocumentController *m_document;
    int m_growPixels = 5; // the width last asked for expand / shrink
};

} // namespace mothion::ui
