#pragma once

#include <QFrame>

namespace mothion::canvas {
class CanvasView;
}

namespace mothion::ui {

// OK and Cancel for the open puppet warp, floating over the canvas under the
// warped area (above it when there is no room below), as the transform's, and
// shown once the warp has something to apply: OK applies it, Cancel drops it
// (Enter and Esc do the same).
class PuppetBar : public QFrame
{
    Q_OBJECT

public:
    explicit PuppetBar(canvas::CanvasView *canvas);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();

    canvas::CanvasView *m_canvas;
};

} // namespace mothion::ui
