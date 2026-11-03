#pragma once

#include "core/document/Document.h"

#include <QString>

#include <memory>

namespace mothion::io {

// Mothion project files (.moth): an SQLite database, so a save is one
// transaction and the file can be inspected with any SQLite tool.
//
//   PRAGMA application_id  'MOTH'
//   PRAGMA user_version    format version (kProjectFormatVersion)
//   document   one row: canvas size, frame rate, scene length, paper color,
//              next ids to allocate, version of Mothion that saved it
//   cels       id, position (0 = bottom), name, visible, locked, opacity, blend
//   drawings   id, cel, name
//   layers     id, drawing, position (0 = bottom), name, visible, locked,
//              opacity, blend
//   exposures  cel, start, length, drawing (the Timing runs)
//   thumbnail  one row: the preview file managers show (ProjectThumbnail.h)
//   tiles      layer, x, y (tile units), pixels: the 64×64 premultiplied
//              ARGB pixels as little-endian uint32, compressed with qCompress
//              (zlib with a 4-byte big-endian length in front)
//
// Format 1 had no layers in drawings: its "layers" table held the cels and
// its tiles belonged to drawings. Such a file opens with each drawing made
// of one layer.
//
// A save writes a new file next to the target and renames it over the old
// one at the end, so a failed save never damages the previous file.

inline constexpr int kProjectFormatVersion = 2;
inline constexpr char kProjectSuffix[] = "moth";
inline constexpr char kOldProjectSuffix[] = "mothion"; // what projects were named before; they still open

// Writes `document` to `path`. On failure returns false and sets `error`.
bool saveProject(const doc::Document &document, const QString &path, QString *error);

// Reads a project. On failure returns nullptr and sets `error`.
std::unique_ptr<doc::Document> loadProject(const QString &path, QString *error);

} // namespace mothion::io
