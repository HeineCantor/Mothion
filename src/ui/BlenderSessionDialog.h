#pragma once

#include "io/BlenderLinkServer.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace mothion::ui {

// Which Blender, and which of its scenes, to bring in as a cel: the ones connected through the add-on, listed as they come and
// go, with where the first frame goes on the timeline. When there is none, how to have one (the add-on is installed from here).
class BlenderSessionDialog : public QDialog
{
    Q_OBJECT

public:
    BlenderSessionDialog(io::BlenderLinkServer *server, int currentFrame, QWidget *parent = nullptr);

    // The session chosen (0: none) and where the first frame goes (from 0).
    int sessionId() const;
    int startFrame() const;

private:
    void fill();
    void installAddOn();

    io::BlenderLinkServer *m_server;
    QListWidget *m_list;
    QLabel *m_empty;
    QSpinBox *m_start;
    QPushButton *m_ok;
};

} // namespace mothion::ui
