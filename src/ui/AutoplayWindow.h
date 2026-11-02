#pragma once

#include "canvas/FrameRenderer.h"
#include "core/document/Ids.h"

#include <QTimer>
#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// A window playing the animation in a loop, frame by frame, as the canvas
// shows it (the visible layers over the paper), from the first frame to
// the last one anything is exposed on. "Play" starts and stops it; the
// slider under the picture follows the playback and, dragged, picks the
// frame; the speed is chosen in frames per second (the document's rate to
// begin with). Space plays and stops too. It plays on its own, apart from
// the canvas's current frame, and follows the document as it is edited.
class AutoplayWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AutoplayWindow(document::DocumentController *document, QWidget *parent = nullptr);

    int frame() const { return m_frame; }
    int frameCount() const { return m_frameCount; } // of the loop
    bool isPlaying() const;
    double fps() const;
    const QImage &image() const { return m_renderer.image(); } // the frame shown

    static const QList<double> &speeds(); // the choices, in fps

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void showFrame(int frame);
    void advance();
    void documentChanged();
    void updateRange();
    void updateTimer();
    // What a frame shows: each layer's drawing, if visible. Frames that show
    // the same are not composed again.
    std::vector<doc::DrawingId> exposedAt(int frame) const;

    document::DocumentController *m_document;
    canvas::FrameRenderer m_renderer;
    QWidget *m_picture;
    QSlider *m_slider;
    QLabel *m_position;
    QCheckBox *m_play;
    QComboBox *m_speed;
    QTimer m_timer;
    QTimer m_refresh; // edits come in bursts (a stroke): composed again once after them
    int m_frame = 0;
    int m_frameCount = 1;
    std::vector<doc::DrawingId> m_shown; // what the composed image shows
    bool m_stale = true;                 // the document changed since it was composed
};

} // namespace mothion::ui
