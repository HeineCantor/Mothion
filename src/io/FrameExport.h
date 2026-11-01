#pragma once

#include "core/document/Document.h"

#include <QImage>
#include <QString>

#include <functional>
#include <optional>

namespace mothion::io {

enum class ImageFormat { Png, Jpeg };

struct ExportOptions
{
    ImageFormat format = ImageFormat::Png;
    bool transparent = false; // PNG only: no paper behind the layers
    int quality = 90;         // JPEG only, 0..100
};

// The format a file name asks for (.png, .jpg, .jpeg), if any.
std::optional<ImageFormat> formatForFile(const QString &path);
QString suffixOf(ImageFormat format); // "png", "jpg"

// What `frame` (0-based) looks like, document-sized: the visible layers
// over the paper, or over transparency.
QImage renderFrame(const doc::Document &document, int frame, bool transparent);

// Writes one frame to `path`.
bool exportFrame(const doc::Document &document, int frame, const QString &path, const ExportOptions &options,
                 QString *error);

// The file of `frame` in a sequence named after `path`: "walk.png" gives
// walk_0001.png, walk_0002.png... numbered 1-based as the timeline shows
// frames, with at least 4 digits (more if `lastFrame` needs them).
QString sequenceFileName(const QString &path, int frame, int lastFrame);

// Writes frames [first, last] (0-based) as a numbered sequence (see
// sequenceFileName). `progress(done, total)` is called after each frame;
// returning false stops the export (that is not an error).
bool exportSequence(const doc::Document &document, int first, int last, const QString &path,
                    const ExportOptions &options, const std::function<bool(int, int)> &progress, QString *error);

} // namespace mothion::io
