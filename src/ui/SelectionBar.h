#pragma once

#include <QFrame>

namespace mothion::canvas {
class CanvasView;
}
namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// Commands for the selection, floating over the canvas a few pixels under
// it (above it when there is no room below), shown only while something is
// selected: deselect, invert, erase, cut and copy to a new layer.
class SelectionBar : public QFrame
{
    Q_OBJECT

public:
    SelectionBar(canvas::CanvasView *canvas, document::DocumentController *document);

signals:
    // A command could not be done; the text says why.
    void statusMessage(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();

    canvas::CanvasView *m_canvas;
    document::DocumentController *m_document;
};

} // namespace mothion::ui
