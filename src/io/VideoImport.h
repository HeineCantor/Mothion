#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace mothion::io {

// The kinds of video ffmpeg is asked to read, as file patterns ("*.mp4", "*.mov"...), for a file dialog.
QStringList videoPatterns();

// What ffmpeg tells of a video before reading it: how long it is (seconds) and its frame rate (frames a second, the
// average one for a video whose rate changes); 0 where it does not say.
struct VideoInfo
{
    double duration = 0;
    double fps = 0;
};
// Empty (both 0) with `error` set if ffmpeg is missing or cannot open it.
VideoInfo probeVideo(const QString &video, QString *error);
// About how many frames the video makes at `fps` (0: every frame of the video, at its own rate).
int videoFrameCount(const VideoInfo &info, double fps);

// The frames of `video` written by ffmpeg (see findFfmpeg()) into `folder` as PNG images, named in order
// (frame_000001.png...), at `fps` frames a second: a video at another rate has frames dropped or held so that it keeps
// its timing (each frame of the scene shows what the video shows at that time); `fps` 0 takes every frame of the video
// as it is, whatever its rate. The first frame is the video's first.
// `progress(done, total)`, in seconds of the video (total 0 while not known), is called as ffmpeg goes; returning false
// stops it (no error: the images written so far are left, and the function returns false with `error` empty).
// The images written, in order; empty with `error` set if the video could not be read.
QStringList extractVideoFrames(const QString &video, const QString &folder, double fps,
                               const std::function<bool(double, double)> &progress, QString *error);

} // namespace mothion::io
