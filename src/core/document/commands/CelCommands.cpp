#include "core/document/commands/CelCommands.h"

#include "core/document/Document.h"

namespace mothion::doc {

namespace {

ChangeSet structural()
{
    ChangeSet c;
    c.structure = true;
    return c;
}

std::size_t celBytes(const Cel *cel)
{
    std::size_t total = sizeof(Cel);
    if (cel)
        for (const auto &[id, d] : cel->drawings())
            total += d.memoryBytes();
    return total;
}

} // namespace

// --- AddCelCommand ------------------------------------------------------------------

AddCelCommand::AddCelCommand(Document &doc, int index, CelProperties properties)
    : m_id(doc.allocateCelId())
    , m_index(index)
    , m_cel(std::make_unique<Cel>(m_id, std::move(properties)))
{
}

ChangeSet AddCelCommand::apply(Document &doc)
{
    if (m_cel)
        doc.insertCel(m_index, std::move(m_cel));
    return structural();
}

ChangeSet AddCelCommand::revert(Document &doc)
{
    m_cel = doc.takeCel(m_id);
    return structural();
}

std::size_t AddCelCommand::memoryBytes() const
{
    return sizeof(*this) + celBytes(m_cel.get());
}

// --- RemoveCelCommand ---------------------------------------------------------------

RemoveCelCommand::RemoveCelCommand(CelId cel)
    : m_id(cel)
{
}

ChangeSet RemoveCelCommand::apply(Document &doc)
{
    m_index = doc.indexOf(m_id);
    m_cel = doc.takeCel(m_id);
    return structural();
}

ChangeSet RemoveCelCommand::revert(Document &doc)
{
    if (m_cel)
        doc.insertCel(m_index, std::move(m_cel));
    return structural();
}

std::size_t RemoveCelCommand::memoryBytes() const
{
    return sizeof(*this) + celBytes(m_cel.get());
}

// --- MoveCelCommand -----------------------------------------------------------------

MoveCelCommand::MoveCelCommand(CelId cel, int newIndex)
    : m_id(cel)
    , m_newIndex(newIndex)
{
}

ChangeSet MoveCelCommand::apply(Document &doc)
{
    m_oldIndex = doc.indexOf(m_id);
    doc.moveCel(m_id, m_newIndex);
    return structural();
}

ChangeSet MoveCelCommand::revert(Document &doc)
{
    doc.moveCel(m_id, m_oldIndex);
    return structural();
}

// --- SetCelPropertiesCommand --------------------------------------------------------

SetCelPropertiesCommand::SetCelPropertiesCommand(CelId cel, CelProperties before, CelProperties after)
    : m_id(cel)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

ChangeSet SetCelPropertiesCommand::put(Document &doc, const CelProperties &properties)
{
    if (Cel *cel = doc.cel(m_id))
        cel->setProperties(properties);
    return structural();
}

ChangeSet SetCelPropertiesCommand::apply(Document &doc)
{
    return put(doc, m_after);
}

ChangeSet SetCelPropertiesCommand::revert(Document &doc)
{
    return put(doc, m_before);
}

std::unique_ptr<Command> makeSetCelProperties(const Document &doc, CelId celId, CelProperties properties)
{
    const Cel *cel = doc.cel(celId);
    if (!cel || cel->properties() == properties)
        return nullptr;
    return std::make_unique<SetCelPropertiesCommand>(celId, cel->properties(), std::move(properties));
}

} // namespace mothion::doc
