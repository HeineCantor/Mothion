#pragma once

#include "core/document/Ids.h"

#include <QWidget>

#include <array>

class QLabel;
class QPushButton;
class QStackedLayout;
class QTimer;
class QToolButton;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// The mouths of a mouth chart (the current cel, see doc::CelProperties::mouthChart), for the lip sync: a picture of
// each shape (A to H, X) with what it is for. A click on one draws it on the canvas, whatever the timeline shows there
// (DocumentController::editMouthDrawing()); a click on it again, or another frame chosen, goes back to the timeline.
// On a cel that is no mouth chart, a button makes it one.
class MouthChartPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MouthChartPanel(document::DocumentController *document, QWidget *parent = nullptr);

    QToolButton *shapeButton(char shape) const; // for tests

private:
    void sync();
    void refreshPictures();

    document::DocumentController *m_document;
    QStackedLayout *m_pages = nullptr;
    QLabel *m_noChart = nullptr;
    QPushButton *m_makeChart = nullptr;
    QLabel *m_hint = nullptr;
    std::array<QToolButton *, 9> m_buttons{};
    QTimer *m_pictureTimer = nullptr; // pictures drawn again once painting pauses
};

} // namespace mothion::ui
