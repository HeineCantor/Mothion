#include "ui/TrajectoryMenu.h"

#include "document/DocumentController.h"

#include <QCoreApplication>
#include <QMenu>

namespace mothion::ui {

namespace {

QString tr(const char *text) { return QCoreApplication::translate("TrajectoryMenu", text); }

} // namespace

void addParentTrajectoryMenu(QMenu &menu, document::DocumentController *document, const doc::Cel &cel, QObject *context)
{
    const doc::Document &d = document->document();
    const doc::CelId celId = cel.id();
    const bool trajectory = cel.properties().trajectory;
    QMenu *parents = menu.addMenu(trajectory ? tr("Parent to") : tr("Follow trajectory"));
    // Its own parent, if that is a trajectory (one bent by a deformer follows the deformer's: that is the deformer's choice).
    const doc::Cel *direct = d.cel(cel.properties().parent);
    const doc::Cel *parent = trajectory ? d.trajectoryParent(cel)
                             : direct && direct->properties().trajectory && direct->id() != celId ? direct
                                                                                                    : nullptr;
    QAction *none = parents->addAction(tr("None"), context,
                                       [document, celId] { document->setTrajectoryParent(celId, doc::kNoId); });
    none->setCheckable(true);
    none->setChecked(!parent);
    // A trajectory under this one (it, or one of its parents, is this cel) would make a loop.
    auto makesLoop = [&](const doc::Cel &t) {
        int guard = 0;
        for (const doc::Cel *p = &t; p && guard <= d.celCount(); p = d.trajectoryParent(*p), ++guard)
            if (p->id() == celId)
                return true;
        return false;
    };
    bool any = false;
    for (int i = d.celCount() - 1; i >= 0; --i) {
        const doc::Cel &t = d.celAt(i);
        if (!t.properties().trajectory || t.id() == celId)
            continue;
        const doc::CelId id = t.id();
        QAction *action = parents->addAction(QString::fromStdString(t.properties().name), context,
                                             [document, celId, id] { document->setTrajectoryParent(celId, id); });
        action->setCheckable(true);
        action->setChecked(parent && parent->id() == id);
        const bool loop = trajectory && makesLoop(t);
        action->setEnabled(!loop);
        if (loop)
            action->setToolTip(tr("It follows this trajectory: it cannot be its parent too"));
        any = any || !loop;
    }
    parents->setToolTipsVisible(true);
    parents->setEnabled(any || parent);

    // A drawn cel can turn as a trajectory's way does, whether it follows one or not.
    if (!cel.properties().drawsNothing() && !cel.properties().model3d) {
        QMenu *turns = menu.addMenu(tr("Turn with path"));
        const doc::Cel *with = d.cel(cel.properties().turnsWith);
        if (with && !with->properties().trajectory)
            with = nullptr;
        QAction *still = turns->addAction(tr("None"), context,
                                          [document, celId] { document->setTurnWithPath(celId, doc::kNoId); });
        still->setCheckable(true);
        still->setChecked(!with);
        bool anyPath = false;
        for (int i = d.celCount() - 1; i >= 0; --i) {
            const doc::Cel &t = d.celAt(i);
            if (!t.properties().trajectory)
                continue;
            const doc::CelId id = t.id();
            QAction *action = turns->addAction(QString::fromStdString(t.properties().name), context,
                                               [document, celId, id] { document->setTurnWithPath(celId, id); });
            action->setCheckable(true);
            action->setChecked(with && with->id() == id);
            action->setToolTip(tr("The drawing turns around its center of mass as that way turns; it is not moved"));
            anyPath = true;
        }
        turns->setToolTipsVisible(true);
        turns->setEnabled(anyPath || with);
    }

    // A drawn cel can be bent by a deformer instead (its parent then is the deformer).
    if (cel.properties().drawsNothing())
        return;
    QMenu *deformers = menu.addMenu(tr("Deformed by"));
    const doc::Cel *bentBy = direct && direct->properties().deformer != doc::DeformerKind::None ? direct : nullptr;
    QAction *unbent = deformers->addAction(tr("None"), context,
                                           [document, celId] { document->setTrajectoryParent(celId, doc::kNoId); });
    unbent->setCheckable(true);
    unbent->setChecked(!bentBy);
    bool anyDeformer = false;
    for (int i = d.celCount() - 1; i >= 0; --i) {
        const doc::Cel &t = d.celAt(i);
        if (t.properties().deformer == doc::DeformerKind::None)
            continue;
        const doc::CelId id = t.id();
        // Bent by it, or carried by one of its parts, whole.
        QMenu *ways = deformers->addMenu(QString::fromStdString(t.properties().name));
        const bool ours = bentBy && bentBy->id() == id;
        const int carriedBy = ours ? cel.properties().parentPart : -2;
        QAction *bend = ways->addAction(tr("Bent by it"), context,
                                        [document, celId, id] { document->setDeformerParent(celId, id, -1); });
        bend->setCheckable(true);
        bend->setChecked(ours && carriedBy < 0);
        const bool bones = t.properties().deformer == doc::DeformerKind::Bones;
        if (!t.deformer().bones.empty())
            ways->addSeparator();
        for (int part = 0; part < int(t.deformer().bones.size()); ++part) {
            QAction *carry = ways->addAction(bones ? tr("Carried by bone %1").arg(part + 1) : tr("Carried by node %1").arg(part + 1),
                                             context, [document, celId, id, part] { document->setDeformerParent(celId, id, part); });
            carry->setCheckable(true);
            carry->setChecked(carriedBy == part);
            carry->setToolTip(tr("Moved and turned as that part is, whole: not bent"));
        }
        ways->setToolTipsVisible(true);
        anyDeformer = true;
    }
    deformers->setEnabled(anyDeformer || bentBy);
}

void addTimeLayerParentMenu(QMenu &menu, document::DocumentController *document, const doc::Cel &cel, doc::LayerId layer,
                            QObject *context)
{
    const doc::TimeLayer *time = cel.timeLayer(layer);
    if (!time)
        return;
    const doc::Document &d = document->document();
    const doc::CelId celId = cel.id();
    const doc::Cel *parent = d.cel(time->parent);
    auto set = [document, celId, layer](doc::CelId parentId, int part) {
        return [document, celId, layer, parentId, part] { document->setTimeLayerParent(celId, layer, parentId, part); };
    };
    QMenu *trajectories = menu.addMenu(tr("Follow trajectory"));
    QAction *none = trajectories->addAction(tr("None"), context, set(doc::kNoId, -1));
    none->setCheckable(true);
    none->setChecked(!parent || !parent->properties().trajectory);
    bool any = false;
    for (int i = d.celCount() - 1; i >= 0; --i) {
        const doc::Cel &t = d.celAt(i);
        if (!t.properties().trajectory)
            continue;
        QAction *action = trajectories->addAction(QString::fromStdString(t.properties().name), context, set(t.id(), -1));
        action->setCheckable(true);
        action->setChecked(parent && parent->id() == t.id());
        any = true;
    }
    trajectories->setEnabled(any || (parent && parent->properties().trajectory));

    QMenu *deformers = menu.addMenu(tr("Deformed by"));
    const doc::Cel *bentBy = parent && parent->properties().deformer != doc::DeformerKind::None ? parent : nullptr;
    QAction *unbent = deformers->addAction(tr("None"), context, set(doc::kNoId, -1));
    unbent->setCheckable(true);
    unbent->setChecked(!bentBy);
    bool anyDeformer = false;
    for (int i = d.celCount() - 1; i >= 0; --i) {
        const doc::Cel &t = d.celAt(i);
        if (t.properties().deformer == doc::DeformerKind::None)
            continue;
        QMenu *ways = deformers->addMenu(QString::fromStdString(t.properties().name));
        const bool ours = bentBy && bentBy->id() == t.id();
        const int carriedBy = ours ? time->parentPart : -2;
        QAction *bend = ways->addAction(tr("Bent by it"), context, set(t.id(), -1));
        bend->setCheckable(true);
        bend->setChecked(ours && carriedBy < 0);
        const bool bones = t.properties().deformer == doc::DeformerKind::Bones;
        if (!t.deformer().bones.empty())
            ways->addSeparator();
        for (int part = 0; part < int(t.deformer().bones.size()); ++part) {
            QAction *carry = ways->addAction(bones ? tr("Carried by bone %1").arg(part + 1) : tr("Carried by node %1").arg(part + 1),
                                             context, set(t.id(), part));
            carry->setCheckable(true);
            carry->setChecked(carriedBy == part);
            carry->setToolTip(tr("Moved and turned as that part is, whole: not bent"));
        }
        ways->setToolTipsVisible(true);
        anyDeformer = true;
    }
    deformers->setEnabled(anyDeformer || bentBy);
}

} // namespace mothion::ui
