#pragma once

#include <QWidget>

class QLayout;

class QSpinBox;

namespace mothion::audio {
class AudioPlayer;
}
namespace mothion::document {
class DocumentController;
}
namespace mothion::tools {
class ToolState;
}

namespace mothion::ui {

class TimelinePlayer;
class TimelineView;

// Timeline panel: a header bar (frame stepping and counter, new frame,
// onion skin, zoom, and a play / pause button that plays the animation on the canvas, with the audio track if
// there is one) and a cel bar above the TimelineView. The onion skin
// button turns it on and off; its arrow opens the settings (frames back and
// on, their colors, opacity). The cel bar adds, deletes and moves cels and
// sets the blend mode and opacity of the current one.
class TimelinePanel : public QWidget
{
    Q_OBJECT

public:
    TimelinePanel(document::DocumentController *document, tools::ToolState *toolState,
                  audio::AudioPlayer *audio = nullptr, QWidget *parent = nullptr);

    TimelineView *view() const { return m_view; }
    TimelinePlayer *player() const { return m_player; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLayout *celBar();
    // Lip sync by hand: a button for each mouth shape (doc::LipSync.h), shown while the current cel is a mouth chart.
    QWidget *mouthBar();
    // The shape put at the current frame (see DocumentController::setMouthShape()), then on by the frames chosen.
    void putMouthShape(char shape);
    bool mouthBarShown() const;

    document::DocumentController *m_document;
    TimelineView *m_view;
    TimelinePlayer *m_player;
    QWidget *m_mouthBar = nullptr;
    QSpinBox *m_mouthStep = nullptr;
};

} // namespace mothion::ui
