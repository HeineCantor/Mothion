#pragma once

#include "core/document/Compositing.h"
#include "core/document/Camera.h"
#include "core/document/Drawing.h"
#include "core/perspective/PerspectiveGrid.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace mothion::io::codec {

// The pieces of the project file format that other things need to write the same way: a tile as it is stored (packed,
// current format), the blend modes by name, and the JSON of a perspective grid, the compositing network and the camera.
// The collaboration sends the same bytes over the network.

QByteArray packTile(const doc::Tile &tile);
std::shared_ptr<doc::Tile> unpackTile(const QByteArray &blob); // null if damaged

QString blendName(doc::BlendMode blend);
doc::BlendMode blendFromName(const QString &text);

QString gridToJson(const perspective::Grid &grid);
perspective::Grid gridFromJson(const QString &text);

QString graphToJson(const doc::GraphData &graph, const doc::GraphView &view);
doc::GraphData graphFromJson(const QString &text);
doc::GraphView viewFromJson(const QString &text);

QString cameraToJson(const doc::Camera &camera);
doc::Camera cameraFromJson(const QString &text);

} // namespace mothion::io::codec
