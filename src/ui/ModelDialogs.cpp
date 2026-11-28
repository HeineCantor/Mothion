#include "ui/ModelDialogs.h"

#include "core/rig3d/Pose.h"
#include "document/DocumentController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

QDoubleSpinBox *angleBox(double low, double high, double value, QWidget *parent)
{
    auto *box = new QDoubleSpinBox(parent);
    box->setRange(low, high);
    box->setDecimals(1);
    box->setSuffix(QStringLiteral("°"));
    box->setValue(value);
    return box;
}

} // namespace

ModelJointDialog::ModelJointDialog(const QString &jointName, const rig3d::JointSettings &s, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Joint “%1”").arg(jointName));
    auto *layout = new QVBoxLayout(this);

    auto *posing = new QGroupBox(tr("Posing"), this);
    auto *form = new QFormLayout(posing);
    m_anchor = new QCheckBox(tr("Anchor: IK does not turn it, nor what it hangs from"), posing);
    m_anchor->setObjectName(QStringLiteral("jointAnchor"));
    m_anchor->setChecked(s.anchor);
    m_follow = new QCheckBox(tr("Follow: turns along when a bone below it is aimed"), posing);
    m_follow->setObjectName(QStringLiteral("jointFollow"));
    m_follow->setChecked(s.follow);
    m_stiffness = new QDoubleSpinBox(posing);
    m_stiffness->setObjectName(QStringLiteral("jointStiffness"));
    m_stiffness->setRange(0, 0.95);
    m_stiffness->setSingleStep(0.05);
    m_stiffness->setValue(s.stiffness);
    m_stiffness->setToolTip(tr("0 follows IK fully; toward 1 it hardly turns"));
    m_softZone = angleBox(0, 90, s.softZone, posing);
    m_softZone->setObjectName(QStringLiteral("jointSoftZone"));
    m_softZone->setToolTip(tr("Within this angle of a limit, IK slows down as it nears it (0: a hard stop only)"));
    form->addRow(m_anchor);
    form->addRow(m_follow);
    form->addRow(tr("Stiffness"), m_stiffness);
    form->addRow(tr("Soft limit zone"), m_softZone);
    layout->addWidget(posing);

    auto *limits = new QGroupBox(tr("Angle limits (from the rest pose)"), this);
    auto *grid = new QGridLayout(limits);
    grid->addWidget(new QLabel(tr("Min"), limits), 0, 1);
    grid->addWidget(new QLabel(tr("Max"), limits), 0, 2);
    auto row = [&](int at, const QString &name, const QString &object, Range &range, bool limited, double lo, double hi,
                   double bound) {
        range.limited = new QCheckBox(name, limits);
        range.limited->setObjectName(object);
        range.limited->setChecked(limited);
        range.min = angleBox(-bound, bound, lo, limits);
        range.max = angleBox(-bound, bound, hi, limits);
        range.min->setEnabled(limited);
        range.max->setEnabled(limited);
        connect(range.limited, &QCheckBox::toggled, range.min, &QWidget::setEnabled);
        connect(range.limited, &QCheckBox::toggled, range.max, &QWidget::setEnabled);
        grid->addWidget(range.limited, at, 0);
        grid->addWidget(range.min, at, 1);
        grid->addWidget(range.max, at, 2);
    };
    row(1, tr("Twist (round the bone, red)"), QStringLiteral("jointTwist"), m_twist, s.limitTwist, s.twistMin, s.twistMax,
        360);
    row(2, tr("Bend toward green"), QStringLiteral("jointBend0"), m_bend[0], s.limitBend[0], s.bendMin[0], s.bendMax[0], 179);
    row(3, tr("Bend toward blue"), QStringLiteral("jointBend1"), m_bend[1], s.limitBend[1], s.bendMin[1], s.bendMax[1], 179);
    m_shape = new QComboBox(limits);
    m_shape->setObjectName(QStringLiteral("jointShape"));
    m_shape->addItem(tr("Stadium (corners rounded)"));
    m_shape->addItem(tr("Rectangle"));
    m_shape->setCurrentIndex(s.rectangle ? 1 : 0);
    m_shape->setToolTip(tr("Where the bone may point when both bends are limited: within both ranges (a rectangle), or the "
                           "same with its short ends rounded, so it cannot bend fully both ways at once"));
    grid->addWidget(new QLabel(tr("Region"), limits), 4, 0);
    grid->addWidget(m_shape, 4, 1, 1, 2);
    auto *hint = new QLabel(tr("In rig mode the chosen joint shows its axes: red along the bone, green and blue across it."),
                            limits);
    hint->setWordWrap(true);
    grid->addWidget(hint, 5, 0, 1, 3);
    layout->addWidget(limits);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

rig3d::JointSettings ModelJointDialog::settings() const
{
    rig3d::JointSettings s;
    s.anchor = m_anchor->isChecked();
    s.follow = m_follow->isChecked();
    s.stiffness = m_stiffness->value();
    s.softZone = m_softZone->value();
    s.limitTwist = m_twist.limited->isChecked();
    s.twistMin = m_twist.min->value();
    s.twistMax = m_twist.max->value();
    for (int a = 0; a < 2; ++a) {
        s.limitBend[a] = m_bend[a].limited->isChecked();
        s.bendMin[a] = m_bend[a].min->value();
        s.bendMax[a] = m_bend[a].max->value();
    }
    s.rectangle = m_shape->currentIndex() == 1;
    return s;
}

BlendShapesDialog::BlendShapesDialog(document::DocumentController *document, doc::CelId cel, QWidget *parent)
    : QDialog(parent)
    , m_document(document)
    , m_cel(cel)
{
    setAttribute(Qt::WA_DeleteOnClose);
    const doc::Cel *c = document->document().cel(cel);
    setWindowTitle(tr("Blend shapes of “%1”").arg(c ? QString::fromStdString(c->properties().name) : QString()));
    auto *layout = new QVBoxLayout(this);
    auto *area = new QScrollArea(this);
    area->setWidgetResizable(true);
    auto *inner = new QWidget(area);
    auto *form = new QFormLayout(inner);
    const rig3d::Model *model = c ? c->model().model() : nullptr;
    if (model)
        for (std::size_t slot = 0; slot < model->morphs.size(); ++slot) {
            auto *slider = new QSlider(Qt::Horizontal, inner);
            slider->setObjectName(QStringLiteral("blendShape%1").arg(slot));
            slider->setRange(0, 100);
            form->addRow(QString::fromStdString(model->morphs[slot].name), slider);
            connect(slider, &QSlider::valueChanged, this, [this, slot](int value) { preview(int(slot), value / 100.0); });
            connect(slider, &QSlider::sliderReleased, this, &BlendShapesDialog::finish);
            m_sliders.push_back(slider);
        }
    if (m_sliders.empty())
        form->addRow(new QLabel(tr("This model has no blend shapes."), inner));
    area->setWidget(inner);
    layout->addWidget(area);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
    resize(360, 320);
    auto follow = [this] {
        if (!m_before) // not while a slider moves: what it shows is its own
            refresh();
    };
    connect(document, &document::DocumentController::currentFrameChanged, this, follow);
    connect(document, &document::DocumentController::motionChanged, this, follow);
    connect(document, &document::DocumentController::documentReplaced, this, &QDialog::close);
    connect(document, &document::DocumentController::structureChanged, this, [this] {
        if (!m_document->document().cel(m_cel))
            close(); // the cel is gone
    });
    refresh();
}

void BlendShapesDialog::refresh()
{
    const doc::Cel *c = m_document->document().cel(m_cel);
    if (!c || c->model().isEmpty())
        return;
    const rig3d::Pose pose = rig3d::Pose::fromNumbers(*c->model().model(),
                                                      m_document->document().deformPoseAt(*c, m_document->currentFrame()));
    for (std::size_t i = 0; i < m_sliders.size() && i < pose.morphs.size(); ++i) {
        const QSignalBlocker block(m_sliders[i]);
        m_sliders[i]->setValue(int(std::lround(pose.morphs[i] * 100)));
    }
}

void BlendShapesDialog::preview(int slot, double weight)
{
    const doc::Cel *c = m_document->document().cel(m_cel);
    if (!c || c->model().isEmpty() || c->properties().locked)
        return;
    const int frame = m_document->currentFrame();
    if (!m_before) {
        m_before = c->motion();
        if (!c->motion().hasKeys(doc::Channel::Deform))
            m_document->beginFirstKey(m_cel, frame);
    }
    rig3d::Pose pose = rig3d::Pose::fromNumbers(*c->model().model(), m_document->document().deformPoseAt(*c, frame));
    if (std::size_t(slot) >= pose.morphs.size())
        return;
    pose.morphs[std::size_t(slot)] = weight;
    doc::CelPose p;
    p.deform = pose.toNumbers();
    m_document->previewCelPose(m_cel, frame, p, {doc::Channel::Deform});
    if (!m_sliders[std::size_t(slot)]->isSliderDown())
        finish(); // a click or a key: one step at once
}

void BlendShapesDialog::finish()
{
    if (!m_before)
        return;
    const doc::CelMotion before = *m_before;
    m_before.reset();
    m_document->finishCelPose(m_cel, before);
}

} // namespace mothion::ui
