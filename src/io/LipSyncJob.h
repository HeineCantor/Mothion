#pragma once

#include "core/document/LipSync.h"

#include <QObject>
#include <QString>

#include <vector>

namespace mothion::io {

// Who hears the sound for the lip sync from the audio.
enum class LipSyncRecognizer {
    Zipa,            // ZIPA, a phone recognizer of any language, run inside Mothion (ZipaLipSync.h)
    RhubarbPhonetic, // Rhubarb Lip Sync by the sounds, any language (RhubarbLipSync.h)
    RhubarbEnglish,  // Rhubarb Lip Sync by English words (PocketSphinx)
};

struct LipSyncOptions
{
    LipSyncRecognizer recognizer = LipSyncRecognizer::Zipa;
    QString dialog;               // what is said, if known: Rhubarb uses it (may be empty)
    QString extendedShapes = QStringLiteral("GHX"); // the shapes past A to F the mouths have (any of G, H and X)
};

// One run of a lip sync from the audio, in the background: its progress told, the mouth cues given at the end.
class LipSyncJob : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void cancel() = 0; // nothing more is told
    virtual bool isRunning() const = 0;

signals:
    void progress(double fraction); // 0..1
    void finished(const std::vector<mothion::doc::MouthCue> &cues);
    void failed(const QString &reason);
};

} // namespace mothion::io
