#pragma once

#include "core/document/Command.h"
#include "core/document/Cel.h"

#include <memory>

namespace mothion::doc {

// Adds a new empty cel at `index` (0 = bottom). The id is allocated at
// construction, so redo recreates the same cel.
class AddCelCommand : public Command
{
public:
    AddCelCommand(Document &doc, int index, CelProperties properties);

    CelId celId() const { return m_id; }

    CommandKind kind() const override { return CommandKind::AddCel; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_id;
    int m_index;
    std::unique_ptr<Cel> m_cel; // held while not in the document
};

// Removes a cel with all its drawings; undo puts it back at the same index.
class RemoveCelCommand : public Command
{
public:
    explicit RemoveCelCommand(CelId cel);

    CommandKind kind() const override { return CommandKind::RemoveCel; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;
    std::size_t memoryBytes() const override;

private:
    CelId m_id;
    int m_index = 0;
    std::unique_ptr<Cel> m_cel;
};

class MoveCelCommand : public Command
{
public:
    MoveCelCommand(CelId cel, int newIndex);

    CommandKind kind() const override { return CommandKind::MoveCel; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;

private:
    CelId m_id;
    int m_newIndex;
    int m_oldIndex = 0;
};

class SetCelPropertiesCommand : public Command
{
public:
    SetCelPropertiesCommand(CelId cel, CelProperties before, CelProperties after);

    CommandKind kind() const override { return CommandKind::ChangeCelProperties; }
    ChangeSet apply(Document &doc) override;
    ChangeSet revert(Document &doc) override;

private:
    ChangeSet put(Document &doc, const CelProperties &properties);

    CelId m_id;
    CelProperties m_before;
    CelProperties m_after;
};

// nullptr if the properties are already `properties`.
std::unique_ptr<Command> makeSetCelProperties(const Document &doc, CelId cel, CelProperties properties);

} // namespace mothion::doc
