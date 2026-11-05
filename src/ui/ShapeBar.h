#pragma once

#include <QFrame>

class QToolButton;

namespace mothion::canvas {
class CanvasView;
}

namespace mothion::ui {

// OK and Cancel for a polygon or curve in progress, floating over the canvas
// under its points (above them when there is no room below), as the
// transform's: OK finishes the shape (Enter does the same), Cancel drops it
// (Esc). OK waits until there are enough points.
class ShapeBar : public QFrame
{
    Q_OBJECT

public:
    explicit ShapeBar(canvas::CanvasView *canvas);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();

    canvas::CanvasView *m_canvas;
    QToolButton *m_ok = nullptr;
};

} // namespace mothion::ui
