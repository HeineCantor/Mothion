#include "ui/BlenderImportDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mothion::ui {

BlenderImportDialog::BlenderImportDialog(const QString &sceneName, int currentFrame, QWidget *parent)
    : QDialog(parent)
    , m_start(new QSpinBox(this))
{
    setObjectName(QStringLiteral("BlenderImportDialog"));
    setWindowTitle(tr("Import Blender scene"));
    m_start->setObjectName(QStringLiteral("start"));
    m_start->setRange(1, 99999);
    m_start->setValue(currentFrame + 1);

    auto *form = new QFormLayout;
    form->addRow(new QLabel(tr("“%1”, seen by its active camera, as one new cel with a frame for each frame of the scene.")
                                .arg(sceneName),
                            this));
    auto *draft = new QLabel(tr("A light draft: Eevee, half the size, with the scene’s compositing and without Line Art."), this);
    draft->setEnabled(false); // dimmed, as a hint
    form->addRow(draft);
    form->addRow(tr("Start at frame"), m_start);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

int BlenderImportDialog::startFrame() const
{
    return m_start->value() - 1;
}

} // namespace mothion::ui
