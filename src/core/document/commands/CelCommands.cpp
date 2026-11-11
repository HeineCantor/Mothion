#include "core/document/commands/CelCommands.h"

#include "core/document/Document.h"
#include "core/document/commands/GroupCommand.h"

#include <algorithm>

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

namespace {

// The steps that turn the stack into `wanted` (both bottom up): each cel that is not where it should be moved there.
std::unique_ptr<Command> reorder(const Document &doc, const std::vector<CelId> &wanted)
{
    std::vector<CelId> now;
    for (int i = 0; i < doc.celCount(); ++i)
        now.push_back(doc.celAt(i).id());
    if (now == wanted)
        return nullptr;
    std::vector<std::unique_ptr<Command>> steps;
    for (std::size_t i = 0; i < wanted.size(); ++i) {
        if (now[i] == wanted[i])
            continue;
        steps.push_back(std::make_unique<MoveCelCommand>(wanted[i], int(i)));
        now.erase(std::find(now.begin(), now.end(), wanted[i]));
        now.insert(now.begin() + std::ptrdiff_t(i), wanted[i]);
    }
    return std::make_unique<GroupCommand>(CommandKind::MoveCel, std::move(steps));
}

} // namespace

std::unique_ptr<Command> makeMoveCels(const Document &doc, const std::vector<CelId> &cels, int insertAt)
{
    std::vector<CelId> order;
    for (int i = 0; i < doc.celCount(); ++i)
        order.push_back(doc.celAt(i).id());
    std::vector<CelId> moving, rest;
    int before = 0; // those of the moving ones that are below the place
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (std::find(cels.begin(), cels.end(), order[i]) != cels.end()) {
            moving.push_back(order[i]);
            if (int(i) < insertAt)
                ++before;
        } else {
            rest.push_back(order[i]);
        }
    }
    if (moving.empty())
        return nullptr;
    const int position = std::clamp(insertAt - before, 0, int(rest.size()));
    rest.insert(rest.begin() + position, moving.begin(), moving.end());
    return reorder(doc, rest);
}

std::unique_ptr<Command> makeShiftCels(const Document &doc, const std::vector<CelId> &cels, bool up)
{
    std::vector<CelId> order;
    std::vector<char> chosen;
    for (int i = 0; i < doc.celCount(); ++i) {
        order.push_back(doc.celAt(i).id());
        chosen.push_back(std::find(cels.begin(), cels.end(), order.back()) != cels.end() ? 1 : 0);
    }
    const int n = int(order.size());
    int first = -1, last = -1; // the lowest and the highest chosen
    for (int i = 0; i < n; ++i)
        if (chosen[std::size_t(i)]) {
            if (first < 0)
                first = i;
            last = i;
        }
    if (first < 0 || (up ? last == n - 1 : first == 0))
        return nullptr;
    // Each chosen cel swaps with the one beyond it, unless that one is chosen too (a block moves as a block).
    if (up) {
        for (int i = n - 2; i >= 0; --i)
            if (chosen[std::size_t(i)] && !chosen[std::size_t(i + 1)]) {
                std::swap(order[std::size_t(i)], order[std::size_t(i + 1)]);
                std::swap(chosen[std::size_t(i)], chosen[std::size_t(i + 1)]);
            }
    } else {
        for (int i = 1; i < n; ++i)
            if (chosen[std::size_t(i)] && !chosen[std::size_t(i - 1)]) {
                std::swap(order[std::size_t(i)], order[std::size_t(i - 1)]);
                std::swap(chosen[std::size_t(i)], chosen[std::size_t(i - 1)]);
            }
    }
    return reorder(doc, order);
}

} // namespace mothion::doc
