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

// How the video is coded. H.264 (.mp4) has no transparency; VP9 (.webm) and ProRes 4444 (.mov) keep it.
enum class VideoCodec { H264, Vp9, ProRes4444 };

// The file suffix of a codec ("mp4", "webm", "mov"), and whether it can hold transparency.
QString videoSuffix(VideoCodec codec);
bool videoHasAlpha(VideoCodec codec);

struct VideoOptions
{
    int crf = 18;             // x264 and VP9 quality, 0 (lossless) to 51: lower is better and bigger (ProRes has none)
    bool compositing = true;  // run the document's compositing network on each frame
    VideoCodec codec = VideoCodec::H264;
    // The paper left out (the layers over nothing): the video keeps the alpha. Only with a codec that has it.
    bool withoutPaper = false;
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

// Writes frames [first, last] (0-based) as a video (H.264 by default, see VideoOptions) at the document's frame rate, through ffmpeg: the
// frames are handed to it one by one, nothing is written to disk but the video. `progress(done, total)` is
// called after each frame; returning false stops the export and removes the file (that is not an error).
// A video with an odd width or height loses one pixel of it: H.264 and VP9 want even sizes. No sound yet.
bool exportVideo(const doc::Document &document, int first, int last, const QString &path, const VideoOptions &options,
                 const std::function<bool(int, int)> &progress, QString *error);

} // namespace mothion::io
