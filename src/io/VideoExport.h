#pragma once

#include "compositing/Evaluator.h"
#include "compositing/Graph.h"

#include <QImage>
#include <QString>

#include <functional>

namespace mothion::doc {
class Document;
}

namespace mothion::io {

struct VideoOptions
{
    int crf = 18;             // x264 quality, 0 (lossless) to 51: lower is better and bigger
    bool compositing = true;  // run the document's compositing network on each frame
};

// The ffmpeg program, if it can be found (the search path, then next to the application); empty if not.
QString findFfmpeg();

// The frame as the video shows it: what renderOutputFrame makes (the page seen through the camera, if there is
// one), at the full size of the page, and, if `graph` is
// given and its Output has something wired to it, what the nodes make of it. Opaque (over the paper) unless
// `transparent`.
// With `transparent` the page is left out (no paper behind the layers) and the result keeps its alpha.
QImage renderFinalFrame(const doc::Document &document, int frame, compositing::Evaluator &evaluator,
                        const compositing::Graph *graph, bool transparent = false);

// Writes frames [first, last] (0-based) as an H.264 .mp4 at the document's frame rate, through ffmpeg: the
// frames are handed to it one by one, nothing is written to disk but the video. `progress(done, total)` is
// called after each frame; returning false stops the export and removes the file (that is not an error).
// A video with an odd width or height loses one pixel of it: H.264 wants even sizes. No sound yet.
bool exportVideo(const doc::Document &document, int first, int last, const QString &path, const VideoOptions &options,
                 const std::function<bool(int, int)> &progress, QString *error);

} // namespace mothion::io
