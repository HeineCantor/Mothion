#pragma once

#include "core/document/Cel.h"
#include "document/Tween.h"

#include <QDialog>
#include <QPointer>

#include <optional>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace mothion::document {
class DocumentController;
}

namespace mothion::ui {

// Asks which two poses of the current cel to make the frames between, and how: from a frame (the current one) to
// another (by default the first one after it that shows another drawing), and the easing. Shows the picture between
// the poses at a place of the way (a slider), made as it will be, so a bad pair is seen before the frames are made.
// The easing is remembered. Frames are shown from 1 and held 0-based.
class TweenDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TweenDialog(document::DocumentController *document, QWidget *parent = nullptr);

    doc::CelId cel() const { return m_cel; }
    int from() const; // 0-based
    int to() const;
    document::TweenEase ease() const;
    // The frames that would be made (0 when the two poses cannot be tweened: the reason is in the dialog).
    int frameCount() const { return m_frames; }
    // The first frame after `frame` of the cel that shows another drawing than the one shown at `frame`, if there is
    // one (what the dialog proposes as the second pose).
    static std::optional<int> nextPose(const doc::Cel &cel, int frame);

    QLabel *preview() const { return m_preview; }
    QPushButton *okButton() const { return m_ok; }
    QSpinBox *fromBox() const { return m_from; }
    QSpinBox *toBox() const { return m_to; }
    QSlider *previewSlider() const { return m_at; }
    void accept() override;

private:
    void poseChanged();   // from or to changed: the poses laid over each other again
    void showPreview();
    void validate();

    document::DocumentController *m_document;
    doc::CelId m_cel = doc::kNoId;
    QSpinBox *m_from = nullptr;
    QSpinBox *m_to = nullptr;
    QComboBox *m_ease = nullptr;
    QSlider *m_at = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_note = nullptr;
    QPushButton *m_ok = nullptr;
    std::optional<document::DrawingTween> m_tween;
    int m_frames = 0;
};

} // namespace mothion::ui
