#include "core/document/commands/TimingCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

SetTimingCommand::SetTimingCommand(LayerId layer, Timing before, Timing after)
    : m_layer(layer)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

ChangeSet SetTimingCommand::put(Document &doc, const Timing &timing)
{
    Layer *layer = doc.layer(m_layer);
    if (!layer)
        return {};
    layer->timing() = timing;
    ChangeSet c;
    c.structure = true;
    return c;
}

ChangeSet SetTimingCommand::apply(Document &doc)
{
    return put(doc, m_after);
}

ChangeSet SetTimingCommand::revert(Document &doc)
{
    return put(doc, m_before);
}

namespace {

// Runs `edit` on a copy of the layer's timing; a command only if it changed.
template<typename Edit>
std::unique_ptr<Command> editTiming(const Document &doc, LayerId layerId, Edit edit)
{
    const Layer *layer = doc.layer(layerId);
    if (!layer)
        return nullptr;
    Timing after = layer->timing();
    edit(after);
    if (after == layer->timing())
        return nullptr;
    return std::make_unique<SetTimingCommand>(layerId, layer->timing(), std::move(after));
}

} // namespace

std::unique_ptr<Command> makeSetExposure(const Document &doc, LayerId layer, int start, int length,
                                         DrawingId drawing)
{
    return editTiming(doc, layer, [&](Timing &t) { t.setExposure(start, length, drawing); });
}

std::unique_ptr<Command> makeResizeExposure(const Document &doc, LayerId layer, int frame, int newLength)
{
    return editTiming(doc, layer, [&](Timing &t) { t.resizeExposure(frame, newLength); });
}

std::unique_ptr<Command> makeInsertEmptyFrames(const Document &doc, LayerId layer, int frame, int count)
{
    return editTiming(doc, layer, [&](Timing &t) { t.insertEmptyFrames(frame, count); });
}

std::unique_ptr<Command> makeRemoveFrames(const Document &doc, LayerId layer, int frame, int count)
{
    return editTiming(doc, layer, [&](Timing &t) { t.removeFrames(frame, count); });
}

} // namespace mothion::doc
