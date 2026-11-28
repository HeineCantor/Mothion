#pragma once

#include "core/document/Document.h"
#include "core/rig3d/JointLimit.h"

#include <QDialog>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSlider;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// What the person sets for a bone of a 3D model, to pose it (see rig3d::JointSettings): anchored, following, how stiff,
// its soft zone, and the limits of its twist and of its two bends with the shape of the region they make.
class ModelJointDialog : public QDialog
{
    Q_OBJECT
public:
    ModelJointDialog(const QString &jointName, const rig3d::JointSettings &settings, QWidget *parent = nullptr);
    rig3d::JointSettings settings() const;

private:
    struct Range
    {
        QCheckBox *limited = nullptr;
        QDoubleSpinBox *min = nullptr;
        QDoubleSpinBox *max = nullptr;
    };
    QCheckBox *m_anchor = nullptr;
    QCheckBox *m_follow = nullptr;
    QDoubleSpinBox *m_stiffness = nullptr;
    QDoubleSpinBox *m_softZone = nullptr;
    Range m_twist, m_bend[2];
    QComboBox *m_shape = nullptr;
};

// The blend shapes of a 3D model cel (its model's morph targets), a slider each: their weights in the pose on the current
// frame. A slider moved goes into the cel's Deform key on the frame by the rules of its keys, as a drag of its bones does;
// one undo step for each move.
class BlendShapesDialog : public QDialog
{
    Q_OBJECT
public:
    BlendShapesDialog(document::DocumentController *document, doc::CelId cel, QWidget *parent = nullptr);

private:
    void refresh();
    void preview(int slot, double weight);
    void finish();

    document::DocumentController *m_document;
    doc::CelId m_cel;
    std::vector<QSlider *> m_sliders;
    std::optional<doc::CelMotion> m_before; // while a slider moves: the motion it began with
};

} // namespace mothion::ui
