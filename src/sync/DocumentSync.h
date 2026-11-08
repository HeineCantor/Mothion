#pragma once

#include "core/document/Ids.h"
#include "core/document/Tile.h"
#include "core/document/Timing.h"
#include "core/document/TiledImage.h"
#include "sync/Wire.h"

#include <QByteArray>

#include <map>
#include <set>
#include <vector>

namespace mothion::document {
class DocumentController;
}

namespace mothion::sync {

// What a shared session needs of the document, on one side: to find what changed in it (as Ops, the state of the
// parts that did) and to put in what changed on the other side.
//
// It remembers what the other side is known to have: a hash of each part's JSON and the tiles of each layer. A scan
// compares the document with that, and what differs goes out as Ops (the drawings and tiles first, then the cels,
// the order of the cels, and last the global things, so that what an Op names is there when it arrives). An Op that
// comes in is put into the document and into what is remembered, so that it does not go back.
//
// What a Cel Op says about its drawings and timing, and what the CelList Op says about the cels, is what changed
// (what was taken out, what was put in), not the whole of it: two people adding a drawing, or a cel, at the same time
// do not undo one another's. A restoring Op (see current()) is the whole, and replaces.
class DocumentSync
{
public:
    explicit DocumentSync(document::DocumentController &controller);

    // The document as it is now is what the other side has (after a project was sent or received).
    void resetBaseline();
    // Everything that changed since the last call, in the order to send it.
    std::vector<Op> collectChanges();
    // A change from the other side, into the document (and the views are told).
    void apply(const Op &op);
    // The Ops that give the part `like` names as the document has it now (to put right a change that was refused);
    // none if there is no such part any more.
    std::vector<Op> current(const Op &like) const;
    // The order the cels have in the document, as a (non-restoring) CelList Op: the host sends it after taking one from
    // somebody, so that everybody ends up with the same order.
    Op celOrder() const;

private:
    std::vector<Op> scan(bool emitOps);
    void rebaseDrawing(doc::CelId cel, doc::DrawingId drawing);

    document::DocumentController &m_controller;
    QByteArray m_globalHash;
    std::vector<doc::CelId> m_celList;
    std::map<doc::CelId, QByteArray> m_celHash;
    std::map<doc::CelId, std::set<doc::DrawingId>> m_celDrawings; // the drawings each cel has, as the other side knows
    std::map<doc::CelId, std::vector<doc::Exposure>> m_celExposures; // and where they are shown
    std::map<doc::DrawingId, QByteArray> m_drawingHash;
    std::map<doc::LayerId, doc::TiledImage> m_images; // the tiles the other side has, by layer
    std::map<doc::LayerId, doc::TiledImage> m_masks;
};

} // namespace mothion::sync
