#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace mothion::io {

// The kinds of video ffmpeg is asked to read, as file patterns ("*.mp4", "*.mov"...), for a file dialog.
QStringList videoPatterns();

// The frames of `video` written by ffmpeg (see findFfmpeg()) into `folder` as PNG images, named in order
// (frame_000001.png...), at `fps` frames a second: a video at another rate has frames dropped or held so that it keeps
// its timing (each frame of the scene shows what the video shows at that time). The first frame is the video's first.
// `progress(done, total)`, in seconds of the video (total 0 while not known), is called as ffmpeg goes; returning false
// stops it (no error: the images written so far are left, and the function returns false with `error` empty).
// The images written, in order; empty with `error` set if the video could not be read.
QStringList extractVideoFrames(const QString &video, const QString &folder, double fps,
                               const std::function<bool(double, double)> &progress, QString *error);

} // namespace mothion::io
