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
    bool compositing = false; // run the document's compositing network on each frame (see io/VideoExport.h)
};

// The format a file name asks for (.png, .jpg, .jpeg), if any.
std::optional<ImageFormat> formatForFile(const QString &path);
QString suffixOf(ImageFormat format); // "png", "jpg"

// With `options.compositing` the frames are what the nodes make of them (if the document has a network with
// something wired to its Output); a transparent PNG then keeps the alpha the nodes leave.

// What `frame` (0-based) looks like, document-sized: the visible layers
// over the paper, or over transparency; perspective layers (guides) left out.
QImage renderFrame(const doc::Document &document, int frame, bool transparent);

// The sheet's picture seen through the camera at `pose`: as big as the sheet, with the part the camera looks at
// carried over it (moved, turned, zoomed with a smooth resampling). Where the camera looks past the sheet's edge
// `background` shows (the paper's color, or transparency).
QImage applyCamera(const QImage &sheet, const doc::CameraPose &pose, QRgb background);

// What a frame looks like to whoever watches the animation, which is what the playback and the exports show:
// renderFrame() seen through the project's camera, if it has one and it is on (visible); the whole sheet if not.
QImage renderOutputFrame(const doc::Document &document, int frame, bool transparent);

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
