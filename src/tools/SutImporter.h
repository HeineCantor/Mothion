#pragma once

#include "tools/BrushPreset.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <optional>

namespace mothion::tools {

// Reads a Clip Studio Paint sub tool (.sut) into a brush preset.
//
// A .sut is an SQLite database: Node names the sub tool and its current
// Variant, the Variant row holds the parameters (numbers, plus binary
// "Effector" blobs for their dynamics, see brush::Effector::parse), and
// MaterialFile embeds each tip image's folder as a tar archive.
//
// The tip images themselves are encrypted in those archives; what is
// readable is a preview thumbnail (at most 300 px), drawn either as line art
// or as an empty shape ringed by a dark glow. tipCoverageFromThumbnail()
// turns it back into a coverage mask. It is a stand-in until real tip images
// can be supplied.
//
// Parameters the engine does not support yet (watercolor, texture, dual
// brush, color jitter...) are listed in `warnings`.
struct SutImport
{
    BrushPreset preset;
    QStringList warnings;
    QList<QImage> tipThumbnails; // as found in the file, in tip order
};

std::optional<SutImport> importSut(const QString &path, QString *error = nullptr);

// Coverage mask (alpha channel) from a .sut tip thumbnail. Thumbnails drawn
// with crisp opaque lines are line art and are used as they are; those with
// a soft glow around an empty shape become the area the glow encloses, with
// the glow's inner half as a soft edge. As in Clip Studio Paint, dark pixels
// are ink and white ones are paper, so opaque white areas paint nothing.
QImage tipCoverageFromThumbnail(const QImage &thumbnail);

} // namespace mothion::tools
