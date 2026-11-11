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
//              opacity, blend, kind ("raster" or "perspective"), perspective
//              (a perspective layer's grid, as a JSON object; else empty)
//   exposures  cel, start, length, drawing (the Timing runs)
//   thumbnail  one row: the preview file managers show (ProjectThumbnail.h)
//   tiles      layer, x, y (tile units), pixels: the 64×64 premultiplied
//              ARGB pixels as little-endian uint32, compressed with qCompress
//              (zlib with a 4-byte big-endian length in front)
//
// Format 1 had no layers in drawings: its "layers" table held the cels and
// its tiles belonged to drawings. Such a file opens with each drawing made
// of one layer. Format 2 had no kinds of layers: all of them are raster.
//
// Format 3 stored each tile's pixels as they are (deflated); format 4 filters them first, with the PNG
// "Paeth" predictor, which makes photographs and shaded drawings a fifth smaller.
//
// Format 5 adds the audio track: the table "audio" (at most one row) holds the sound file whole, as it was, and
// where and how it plays. Older files have none.
//
// Format 6 adds to it whether the sound is heard while the timeline is scrubbed (older files: it is).
//
// Format 7 adds the compositing network: the table "compositing" (at most one row) holds it as JSON text (see
// GraphData). Older files have none.
//
// Format 8 adds the camera: the table "camera" (at most one row) holds it as JSON text (see doc::Camera). Older
// files have none.
//
// Format 9 adds the masks of layers: the tables "masks" (the layers that have one) and "masktiles" (its pixels).
// Older files have none.
//
// Format 12 adds the Blender link of a cel rendered from a scene: the table "blender_links". Older files have none.
//
// Format 13 adds the layers clipped to the one below: the table "clipped_layers". Older files have none.
//
// Format 14 adds the trajectory cels (cels that are not drawn, only moved by their keys): the table "trajectory_cels".
// Older files have none.
//
// A save writes a new file next to the target and renames it over the old
// one at the end, so a failed save never damages the previous file.

inline constexpr int kProjectFormatVersion = 14;
inline constexpr char kProjectSuffix[] = "moth";
inline constexpr char kOldProjectSuffix[] = "mothion"; // what projects were named before; they still open

// Writes `document` to `path`. On failure returns false and sets `error`.
bool saveProject(const doc::Document &document, const QString &path, QString *error);

// Reads a project. On failure returns nullptr and sets `error`.
std::unique_ptr<doc::Document> loadProject(const QString &path, QString *error);

} // namespace mothion::io
