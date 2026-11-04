#pragma once

#include <QFrame>

namespace mothion::canvas {
class CanvasView;
}

namespace mothion::ui {

// OK and Cancel for the open transform, floating over the canvas under the
// transform box (above it when there is no room below), like the selection's
// commands, and shown only while a transform is open: OK applies it, Cancel
// drops it (Enter and Esc do the same).
class TransformBar : public QFrame
{
    Q_OBJECT

public:
    explicit TransformBar(canvas::CanvasView *canvas);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();

    canvas::CanvasView *m_canvas;
};

} // namespace mothion::ui
