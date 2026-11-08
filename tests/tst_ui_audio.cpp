// Tests for the UI layer: the audio track and its playback.

#include "test_common.h"

class TestUiAudio : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_settingsDir;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // keep the user's brushes out
        // And the user's settings (recent projects...): a folder of their own.
        QCoreApplication::setOrganizationName(QStringLiteral("MothionTest"));
        QCoreApplication::setApplicationName(QStringLiteral("MothionTest"));
        QVERIFY(m_settingsDir.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, m_settingsDir.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }


    // The timeline's header has a button for Import audio, which asks the window for the file as the menu does.
    void timelineHasAnImportAudioButton()
    {
        tools::ToolState state;
        document::DocumentController controller;
        ui::TimelinePanel panel(&controller, &state);
        panel.show();
        auto *button = panel.findChild<QToolButton *>(QStringLiteral("importAudio"));
        QVERIFY(button);
        QSignalSpy asked(panel.view(), &ui::TimelineView::audioImportRequested);
        button->click();
        QCOMPARE(asked.count(), 1);
    }


    // The audio track: the file is read into the project, offset / volume / mute are undo steps, it can be
    // taken away, and an unreadable file is refused with a reason.
    void audioTrackIsKeptInTheProject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile song(dir.filePath(QStringLiteral("Theme Song.MP3")));
        QVERIFY(song.open(QIODevice::WriteOnly));
        song.write(QByteArray(5000, 'x'));
        song.close();
        document::DocumentController controller;
        QVERIFY(!controller.audioTrack());
        QString reason;
        QVERIFY(!controller.importAudio(dir.filePath(QStringLiteral("missing.mp3")), &reason));
        QVERIFY(!reason.isEmpty());

        QVERIFY(controller.importAudio(song.fileName(), &reason));
        QVERIFY(controller.audioTrack());
        QCOMPARE(QString::fromStdString(controller.audioTrack()->name), QStringLiteral("Theme Song"));
        QCOMPARE(QString::fromStdString(controller.audioTrack()->format), QStringLiteral("mp3"));
        QCOMPARE(controller.audioTrack()->bytes(), std::size_t(5000));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo audio"));
        QVERIFY(controller.isModified());

        controller.setAudioOffset(24);
        controller.setAudioVolume(0.5f);
        controller.setAudioMuted(true);
        controller.setAudioMuted(true); // the same: no step
        QCOMPARE(controller.audioTrack()->offset, 24);
        QCOMPARE(controller.audioTrack()->volume, 0.5f);
        QVERIFY(controller.audioTrack()->muted);
        controller.undo();
        QVERIFY(!controller.audioTrack()->muted);
        controller.undo();
        QCOMPARE(controller.audioTrack()->volume, 1.0f);
        controller.undo();
        QCOMPARE(controller.audioTrack()->offset, 0);
        controller.setAudioDuration(61.5); // a fact about the file: not a step
        QCOMPARE(controller.audioTrack()->duration, 61.5);
        controller.undo(); // the import itself
        QVERIFY(!controller.audioTrack());
        controller.redo();
        QVERIFY(controller.audioTrack());
        controller.removeAudio();
        QVERIFY(!controller.audioTrack());
        controller.undo();
        QVERIFY(controller.audioTrack()); // back with its sound
        QCOMPARE(controller.audioTrack()->bytes(), std::size_t(5000));
    }


    // A sound file is decoded in the background into its length and a peak for each hundredth of a second: here
    // a WAV of one second of silence and one of a sine at half scale; a file that is not sound fails.
    void audioAnalyzerReadsTheLoudness()
    {
        constexpr int rate = 22050;
        QByteArray pcm;
        for (int i = 0; i < 2 * rate; ++i) {
            const qint16 v = i < rate ? 0 : qint16(16384 * std::sin(2.0 * 3.14159265358979 * 440.0 * i / rate));
            pcm.append(reinterpret_cast<const char *>(&v), 2);
        }
        QByteArray wav;
        QDataStream out(&wav, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + pcm.size());
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(pcm.size());
        out.writeRawData(pcm.constData(), pcm.size());

        audio::AudioAnalyzer analyzer;
        QSignalSpy finished(&analyzer, &audio::AudioAnalyzer::finished);
        QSignalSpy failed(&analyzer, &audio::AudioAnalyzer::failed);
        const auto data = std::make_shared<const std::vector<unsigned char>>(wav.cbegin(), wav.cend());
        analyzer.analyze(data, data.get());
        QVERIFY(finished.wait(10000));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(finished.first().at(0).value<const void *>(), static_cast<const void *>(data.get()));
        const auto wave = finished.first().at(1).value<audio::Waveform>();
        QVERIFY(std::abs(wave.duration - 2.0) < 0.05);
        QVERIFY(wave.peaks.size() >= 195 && wave.peaks.size() <= 205);
        QVERIFY(wave.peaks[50] < 0.02f);                           // the silence
        QVERIFY(wave.peaks[150] > 0.4f && wave.peaks[150] < 0.6f); // the sine, at half scale

        const auto junk = std::make_shared<const std::vector<unsigned char>>(std::vector<unsigned char>(2000, 'x'));
        finished.clear();
        analyzer.analyze(junk, junk.get());
        QVERIFY(failed.count() > 0 || failed.wait(10000)); // at once, or when the backend gives up
        QCOMPARE(finished.count(), 0);
    }


    // The audio track is a row at the bottom of the timeline: a mute button in its header, a block that is
    // dragged along the frames (one undo step), the waveform once the sound is read, and no row without a track.
    void timelineHasAnAudioRowAtTheBottom()
    {
        tools::Preferences::instance().resetAll();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        constexpr int rate = 22050;
        QByteArray pcm(rate * 2 * 2, '\0'); // two seconds of silence, 16 bits
        QByteArray wav;
        QDataStream out(&wav, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + pcm.size());
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(pcm.size());
        out.writeRawData(pcm.constData(), pcm.size());
        QFile file(dir.filePath(QStringLiteral("song.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(wav);
        file.close();

        document::DocumentController controller;
        ui::TimelineView view(&controller);
        view.resize(600, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        // The cel's row is 22 px under the ruler, the audio row follows it; frames are 16 px, from x = 130.
        const int audioY = ui::TimelineMetrics{}.rulerHeight + 22;
        QWidget *viewport = view.viewport();
        QTest::mouseClick(viewport, Qt::LeftButton, {}, QPoint(20, audioY + 20)); // no track: nothing here
        QVERIFY(!controller.audioTrack());

        QString reason;
        QVERIFY(controller.importAudio(file.fileName(), &reason));
        QTRY_VERIFY(controller.audioTrack()->duration > 1.5); // read in the background
        QCOMPARE(controller.undoText(), QStringLiteral("Undo audio")); // the length is not a step

        QVERIFY(!controller.audioTrack()->muted);
        QTest::mouseClick(viewport, Qt::LeftButton, {}, QPoint(20, audioY + 12)); // the speaker
        QVERIFY(controller.audioTrack()->muted);
        QTest::mouseClick(viewport, Qt::LeftButton, {}, QPoint(20, audioY + 12));
        QVERIFY(!controller.audioTrack()->muted);

        // Next to the speaker, where a cel has its lock: the scrubbing button.
        QVERIFY(controller.audioTrack()->scrub);
        QTest::mouseClick(viewport, Qt::LeftButton, {}, QPoint(38, audioY + 12));
        QVERIFY(!controller.audioTrack()->scrub);
        QTest::mouseClick(viewport, Qt::LeftButton, {}, QPoint(38, audioY + 12));
        QVERIFY(controller.audioTrack()->scrub);

        // The volume slider under them (x 12..101): heard as it is dragged, one undo step when it is let go.
        QCOMPARE(controller.audioTrack()->volume, 1.0f);
        QTest::mousePress(viewport, Qt::LeftButton, {}, QPoint(101, audioY + 32));
        QTest::mouseMove(viewport, QPoint(12 + 45, audioY + 32)); // about the middle
        QVERIFY(std::abs(controller.audioTrack()->volume - 0.5f) < 0.06f);
        QTest::mouseMove(viewport, QPoint(12 + 20, audioY + 32));
        QTest::mouseMove(viewport, QPoint(12 + 45, audioY + 32));
        QTest::mouseRelease(viewport, Qt::LeftButton, {}, QPoint(12 + 45, audioY + 32));
        QCOMPARE(controller.undoText(), QStringLiteral("Undo audio change"));
        controller.undo(); // one undo for the whole drag, back to where it began (the scrub setting kept)
        QCOMPARE(controller.audioTrack()->volume, 1.0f);
        QVERIFY(controller.audioTrack()->scrub);
        controller.redo();
        QVERIFY(std::abs(controller.audioTrack()->volume - 0.5f) < 0.06f);
        controller.setAudioVolume(1.0f);

        // The block from frame 0: 2 s at 24 fps is 48 frames. Dragged 5 frames on.
        QTest::mousePress(viewport, Qt::LeftButton, {}, QPoint(200, audioY + 20));
        QTest::mouseMove(viewport, QPoint(200 + 5 * 16, audioY + 20));
        QCOMPARE(controller.audioTrack()->offset, 0); // not until it is dropped
        QTest::mouseRelease(viewport, Qt::LeftButton, {}, QPoint(200 + 5 * 16, audioY + 20));
        QCOMPARE(controller.audioTrack()->offset, 5);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo audio change"));
        controller.undo();
        QCOMPARE(controller.audioTrack()->offset, 0);

        // Outside the block, the row scrubs.
        QTest::mouseClick(viewport, Qt::LeftButton, {}, QPoint(130 + 60 * 16, audioY + 20));
        QCOMPARE(controller.currentFrame(), 60);
        controller.removeAudio();
        QTest::mouseClick(viewport, Qt::LeftButton, {}, QPoint(20, audioY + 12)); // gone: no speaker to click
        QVERIFY(!controller.audioTrack());
    }


    // Where the sound is at a frame: seconds from its start, negative before it begins.
    void audioPlayerKnowsWhereTheSoundIs()
    {
        QCOMPARE(audio::AudioPlayer::secondsAt(0, 0, 24.0), 0.0);
        QCOMPARE(audio::AudioPlayer::secondsAt(48, 0, 24.0), 2.0);
        QCOMPARE(audio::AudioPlayer::secondsAt(48, 24, 24.0), 1.0); // it begins at frame 25
        QVERIFY(audio::AudioPlayer::secondsAt(10, 24, 24.0) < 0.0);
    }


    // The sound plays from a frame on, stops, and is not heard muted or at volume 0 or without a track.
    void audioPlayerPlaysAndStops()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("song.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4000, 'x')); // the player reads it in the background; here it is only a track
        file.close();
        document::DocumentController controller;
        audio::AudioPlayer player(&controller);
        player.play(5);
        QVERIFY(!player.isPlaying()); // no track
        QVERIFY(controller.importAudio(file.fileName()));
        QVERIFY(player.isAudible());
        player.play(5);
        QVERIFY(player.isPlaying());
        player.stop();
        QVERIFY(!player.isPlaying());
        controller.setAudioMuted(true);
        QVERIFY(!player.isAudible());
        player.play(5);
        QVERIFY(!player.isPlaying());
        controller.setAudioMuted(false);
        player.play(5);
        QVERIFY(player.isPlaying());
        controller.setAudioMuted(true); // muted while playing: it stops
        QVERIFY(!player.isPlaying());
        controller.setAudioMuted(false);
        controller.setAudioVolume(0.0f);
        player.play(5);
        QVERIFY(!player.isPlaying());
        controller.setAudioVolume(1.0f);
        player.play(5);
        controller.removeAudio();
        QVERIFY(!player.isPlaying()); // the track is gone
    }


    // Muting (or silencing) while the animation plays and unmuting again: the sound comes back at the frame it is at,
    // on the next beat of the playback, without a stop and a play.
    void soundComesBackAfterMutingWhilePlaying()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("song.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4000, 'x'));
        file.close();
        document::DocumentController controller;
        QVERIFY(controller.importAudio(file.fileName()));
        audio::AudioPlayer player(&controller);
        player.play(5);
        QVERIFY(player.isPlaying() && player.isRequested());
        controller.setAudioMuted(true);
        QVERIFY(!player.isPlaying()); // quiet...
        QVERIFY(player.isRequested()); // ...but the playback goes on
        player.resync(8);
        QVERIFY(!player.isPlaying()); // still muted
        controller.setAudioMuted(false);
        QVERIFY(!player.isPlaying());
        player.resync(9); // the next frame of the playback
        QVERIFY(player.isPlaying()); // back, from there
        player.setSilenced(true);
        QVERIFY(!player.isPlaying() && player.isRequested());
        player.setSilenced(false);
        player.resync(12);
        QVERIFY(player.isPlaying());
        player.stop();
        player.resync(13);
        QVERIFY(!player.isPlaying() && !player.isRequested()); // stopped is stopped
    }


    void audioScrubbingCanBeTurnedOff()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("song.wav")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4000, 'x'));
        file.close();
        document::DocumentController controller;
        QVERIFY(controller.importAudio(file.fileName()));
        QVERIFY(controller.audioTrack()->scrub); // on to begin with
        controller.setAudioScrub(false);
        QVERIFY(!controller.audioTrack()->scrub);
        QCOMPARE(controller.undoText(), QStringLiteral("Undo audio change"));
        controller.setAudioScrub(false); // the same: no step
        controller.undo();
        QVERIFY(controller.audioTrack()->scrub);
        controller.redo();
        QVERIFY(!controller.audioTrack()->scrub);
    }


    // An mp3 is decoded too (the format of the samples is left to the file: asking for another once made it never
    // end): its length and loudness come out, as for a WAV.
    void audioAnalyzerReadsAnMp3()
    {
        const QByteArray mp3 = QByteArray::fromBase64(QByteArray( // 0.6 s of a 440 Hz sine, stereo, 44.1 kHz, 32 kbps (as the usual mp3s)
            "//sQZAAAAFsEXMUMAA4AAA0goAABBKgzXLmhAAAAADSDAAAAABYAAHd+IEQTHH5geSQAAI3CO9MBeNYBogDRYkJcNjZS605/r/98"
            "KA+Ag1wqCviI8qtV2Y48Rsu6aQQhAKul2n+d6m3/+xJkCY/wlAbUB2wACAAADSDgAAECNBtOBm9CIAAANIAAAARgzBAGgptXKecK"
            "mo78bjD709VpadByIQY0TwNDBGWkpJAFHefMATBgEbbtHqBJru5GI2+lIqWVQyyILBEy7STtEVNkMiX/+xBkGY/wiAdUAZswmAAA"
            "DSAAAAECNBtOBm9CIAAANIAAAAQEmszh4Iwouc1l8OruoG0aAjnVyqyp9RUCMy2jxCa6dKQKjnD2QAa5DX2A6w64YQIyCCqrTSl0"
            "hECTHMDPYdnRtEQElv/7EmQqA/CUB1MDmkkIAAANIAAAAQH0G1ik6SJgAAA0gAAABGwRggiealFg6jXoGyNAI53KrKnRFQQyriOk"
            "JyTsSAKOTQAmiYarAA6kXuGCBGIIKqtaUukKAkxjCzuJa0bqICSzYIwwkP/7EGQ7A/B9BtSDeTEIAAANIAAAAQHwG1ijaSJgAAA0"
            "gAAABOdbyEq16BsjQCOdzgAFmUwpR2MUrgMTQydiQSjjZIBtCQbTBA7IV+BBARgooqWVQCvoRBcx3bT4VXMdeSho1nuFsKCz//sS"
            "ZE0P8IkHVAOZMQgAAA0gAAABAfAbUgTtImAAADSAAAAEZyVKBx9CAKyWVBW3ppTAS+RULGNLgeazATjjQZM7yssAPAQIdbXANQFg"
            "cBhIJYOMKozEm4o7CAMTFONTnL0biGrH0Kjb//sQZF8P8H0G1IN5MQgAAA0gAAABAfAbUgTtImAAADSAAAAErAbKABqCYRcuI15j"
            "rV5gs+pPH6TIDaFw8MVqOOm6L6vRIHmabCewRHowk4GDDzaADxgsHsEdh92eRDcKAASmJPCm8KD/+xJkcQ/whgdUA5kxCAAADSAA"
            "AAEB3BtSBOkiYAAANIAAAASkxn9TunT2FZuRR2I3RgN0ABIMbBiOAMg84iFsbYk0XRWICuAAMSJXRQodgBdFko6Cg+Gw+mvxVs6o"
            "BJ5jIHH6Y8fcCAP/+xBkg4PwiQbVqZswmAAADSAAAAECEBtQBO0iYAAANIAAAASM4OQjwnuOGeAsMACwSNAw7MBiDAgHRMX/p0xc"
            "ByzALB+MOsHTjZIDeGrmo0d7v6i9RQdDp52BRh0grmzcZibI4L5hyP/7EmSUj/CRB1MDmUkIAAANIAAAAQIYG0wGbYJgAAA0gAAA"
            "BASHRUmTTmAUBWyKjYhdFf/01QaACw0QpgtjMAkWIw4rSDDZFcCwLoA0GQJ3BuSKr3XlkBpESGngDLmGQBoa1JEhrCAeGP/7EGSl"
            "j/CPB1MDmUkIAAANIAAAAQJIG0oGbYJgAAA0gAAABGOAucI4Yo8CUIilDgaP3KlAiABAIAAJTHo62IqA0KyUKNbItN10SoZRP//v"
            "/+gHChpSPYwPQMQ8C03Yh4zczA7MQgCA//sSZLUD8KIHUYO6YQgAAA0gAAABAgAbdoM8wrAAADSAAAAE760y68AMhHrIScEXRX9S"
            "BIQQExVgMdxCMwYMGdhgtjJBcG8ypgNARfj5pNC2e8sEARY8UURdmYdAAhs9hVGygAkYcoCZ//sQZMUL8KsHUQH90IgAAA0gAAAB"
            "ApAdSKM/YCAAADSAAAAEzkBjkgVUjloqDJLcFv1VAgSFxoYOyxFDo6451+Fx0TAGCENXM1wxswpjK2WrUZn9YODCSEepE7EOHSNn"
            "oKQWyjMOQAr/+xJk0gPwsQdSK5lhCAAADSAAAAECZB1ogyXgMAAANIAAAAQ558xqULKCrbJBseu0qgSDCAbIToZiwAOsYPPXhg8D"
            "oGAqEKbGRsCCZ4vATYNXolkFRI4axF/jCsALNPcTsBTWgoVM9Dj/+xBk34PwqwdVoftgmAAADSAAAAEDEBs8DHMCaAAANIAAAATA"
            "UFWSX4hCmRX/+6oAAgBAAAAH1bsm2SQFRrMJvtNPpEA5Htl7FFvufeNf/3AIkDloRiPQBMQ0D43XizDcvAuAxBh20P/7EmTqj/Eu"
            "CMqDfsiYAAANIAAAAQSsHyoNe0JAAAA0gAAABJj1YowFNYwTgK6K/roGgwoMkB0CYswGh1DEz6sMSwc4wLQhTg2NogJPBbg9Y1Oi"
            "NfqIAAeADuPo7IBCzGUc/NTP0OjEAf/7EGTnD/ECB8uDXsCaAAANIAAAAQRQHywNe0JoAAA0gAAABJGhkC6E5G7RTAHqACK2w2Aw"
            "GAwGAAAAAA5G+Nl4X0XvZ+TZHm1ys6XJZvDlye/IIOM5/k4VzAuf+fMDQCfgkGJVqAAB//sSZOeD8PAHVFOYYRwAAA0gAAABBJQf"
            "KA17QmAAADSAAAAEiQMAgEk4KASSUFBQWkxBTUUzLjEwMSAoYmV0YSAzKaqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"
            "qqqqqqqq//sQZOiP8QEHywNeyJoAAA0gAAABBIgfKg17QmAAADSAAAAEqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"
            "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqr/+xJk6A/xCwfKg37ImAAADSAAAAEEGB8qDXtCaAAANIAAAASqqqqq"
            "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqr/+xBk6Q/xDgfK"
            "g37ImgAADSAAAAEEEB0uDPsiYAAANIAAAASqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"
            "qqqqqqqqqqqqqqqqqqqqqqqqqv/7EmTpi/EFB0/THdCIAAANIAAAAQR8HygNe0JgAAA0gAAABKqqqqqqqqqqqqqqqqqqqqqqqqqq"
            "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqv/7EGTpg/EiB8qDfsiYAAANIAAAAQPAH0iV"
            "sAAoAAA0goAABKqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"
            "qqqq//sSZOoAAaYdXu4+AAQAAA0gwAAAAhgVZxxggCgAADSDgAAEqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"
            "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq"));
        QVERIFY(mp3.size() > 1000);
        const auto data = std::make_shared<const std::vector<unsigned char>>(mp3.cbegin(), mp3.cend());
        audio::AudioAnalyzer analyzer;
        QSignalSpy finished(&analyzer, &audio::AudioAnalyzer::finished);
        QSignalSpy failed(&analyzer, &audio::AudioAnalyzer::failed);
        analyzer.analyze(data, data.get());
        QVERIFY(finished.wait(20000) || finished.count() > 0);
        QCOMPARE(failed.count(), 0);
        const auto wave = finished.first().at(1).value<audio::Waveform>();
        QVERIFY(std::abs(wave.duration - 0.6) < 0.15);
        QVERIFY(wave.peaks.size() >= 50);
        float loudest = 0.0f;
        for (float peak : wave.peaks)
            loudest = std::max(loudest, peak);
        QVERIFY(loudest > 0.3f); // the tone is there
    }

};

QTEST_MAIN(TestUiAudio)
#include "tst_ui_audio.moc"
