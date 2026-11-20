#pragma once

#include "core/document/Audio.h"
#include "io/LipSyncJob.h"

#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QProcess;
class QTemporaryDir;

namespace mothion::io {

// Lip sync from the sound with Rhubarb Lip Sync (https://github.com/DanielSWolf/rhubarb-lip-sync, MIT), a program of
// its own that Mothion runs: it hears the audio track and says which mouth shape (A to H, X) goes from when to when.

// Where Rhubarb is: the program named by MOTHION_RHUBARB, then the one that comes with Mothion (in "rhubarb" beside it,
// with its "res" folder), then one beside Mothion, then one on the PATH. Empty if there is none.
QString findRhubarb();

// The cues of Rhubarb's JSON output ("mouthCues"); nothing, with `error`, if it is not that.
std::optional<std::vector<doc::MouthCue>> parseRhubarbJson(const QByteArray &json, QString *error = nullptr);

// A line of Rhubarb's machine-readable status (--machineReadable, on its stderr): its progress (0..1), or why it failed.
struct RhubarbStatus
{
    std::optional<double> progress;
    std::optional<QString> failure;
};
RhubarbStatus parseRhubarbStatus(const QByteArray &line);

// One run of Rhubarb on an audio track, in the background: the sound written out (a WAV or an Ogg file as it is,
// another kind made a WAV by ffmpeg first), Rhubarb run on it (by English words when the options' recognizer is
// RhubarbEnglish, else by its sounds), its progress told, its cues given at the end.
class RhubarbJob : public LipSyncJob
{
    Q_OBJECT

public:
    explicit RhubarbJob(QObject *parent = nullptr);
    ~RhubarbJob() override;

    // False, with `reason`, if it cannot begin (no Rhubarb, no sound, a file that cannot be written). `rhubarb`: the
    // program (findRhubarb() if empty).
    bool start(const doc::AudioTrack &track, const LipSyncOptions &options, QString *reason,
               const QString &rhubarb = {});
    void cancel() override;
    bool isRunning() const override;

private:
    void runRhubarb(const QString &soundFile);
    void fail(const QString &reason);

    std::unique_ptr<QTemporaryDir> m_dir;
    QProcess *m_process = nullptr;
    QString m_rhubarb;
    LipSyncOptions m_options;
    QByteArray m_status; // what came on stderr and is not a whole line yet
    QString m_failure;   // why it failed, as Rhubarb said
};

} // namespace mothion::io
