#pragma once

#include "core/document/Cel.h"

class QMenu;
class QObject;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// The menus of a cel's parent, for the menus of a cel (the timeline's and the Layers panel's): a cel that is
// no trajectory gets "Follow trajectory", a trajectory "Parent to" (it becomes the child of another, and moves along its
// way). Both list the trajectories, the parent checked, with None to let go; a trajectory that would make a loop (the
// cel itself, or one that follows it) is shown but cannot be chosen. A drawn cel also gets "Deformed by": the deformation
// cels, each to bend it or to carry it by one of its bones (or nodes), whole, with None. `context` owns the actions' connections.
void addParentTrajectoryMenu(QMenu &menu, document::DocumentController *document, const doc::Cel &cel, QObject *context);
// The same for a time layer of `cel` (see doc::TimeLayer): "Follow trajectory" and "Deformed by", what it follows of its own
// before what its cel does.
void addTimeLayerParentMenu(QMenu &menu, document::DocumentController *document, const doc::Cel &cel, doc::LayerId layer,
                            QObject *context);

} // namespace mothion::ui
