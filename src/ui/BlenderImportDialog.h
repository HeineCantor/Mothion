#pragma once

#include "io/BlenderRender.h"

#include <QDialog>

class QSpinBox;

namespace mothion::ui {

// The options of a Blender scene about to be imported as a cel: where the first frame goes on the timeline (it is a draft,
// see io::BlenderRender).
class BlenderImportDialog : public QDialog
{
    Q_OBJECT

public:
    BlenderImportDialog(const QString &sceneName, int currentFrame, QWidget *parent = nullptr);

    int startFrame() const; // from 0

private:
    QSpinBox *m_start;
};

} // namespace mothion::ui
