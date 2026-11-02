#pragma once

#include "core/document/Ids.h"
#include "core/document/Tile.h"

#include <cstddef>
#include <vector>

namespace mothion::doc {

class Document;

// What a command changed, so views refresh only what they must.
struct ChangeSet
{
    struct Pixels
    {
        LayerId layer = kNoId;
        DrawingId drawing = kNoId;
        std::vector<TileCoord> tiles;
    };

    bool structure = false;     // layers, drawings, timing or document properties
    std::vector<Pixels> pixels; // painted tiles

    bool isEmpty() const { return !structure && pixels.empty(); }
    void merge(ChangeSet other)
    {
        structure = structure || other.structure;
        for (Pixels &p : other.pixels)
            pixels.push_back(std::move(p));
    }
};

// For UI labels ("Undo stroke"); the core stays free of display strings.
enum class CommandKind {
    Paint,
    AddDrawing,
    RemoveDrawing,
    ChangeTiming,
    AddLayer,
    RemoveLayer,
    MoveLayer,
    ChangeLayerProperties,
    ImportImage,
    ClearDrawing,
    Move,
    Select,
    CutToLayer,
    CopyToLayer,
    Fill,
    Transform,
    Paste,
};

// An undoable change to a Document.
//
// apply() does (and redoes) the change, revert() undoes it; each is called
// only when the document is in the matching state, as History guarantees.
// Anything a command needs to redo exactly (new ids, removed layers or
// drawings, old tiles) is captured when it is created or first applied.
class Command
{
public:
    virtual ~Command() = default;

    virtual CommandKind kind() const = 0;
    virtual ChangeSet apply(Document &doc) = 0;
    virtual ChangeSet revert(Document &doc) = 0;

    // Approximate memory kept alive by this command (for History limits).
    virtual std::size_t memoryBytes() const { return 256; }
};

} // namespace mothion::doc
