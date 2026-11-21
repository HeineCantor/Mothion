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
        QAction *action = deformers->addAction(QString::fromStdString(t.properties().name), context,
                                               [document, celId, id] { document->setTrajectoryParent(celId, id); });
        action->setCheckable(true);
        action->setChecked(bentBy && bentBy->id() == id);
        anyDeformer = true;
    }
    deformers->setEnabled(anyDeformer || bentBy);
}

} // namespace mothion::ui
