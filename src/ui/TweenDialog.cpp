#include "ui/TweenDialog.h"

#include "document/DocumentController.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QThreadPool>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace mothion::ui {

namespace {

const QString kEaseKey = QStringLiteral("tween/ease");
constexpr int kMaxFrame = 99999;
constexpr QSize kPreviewSize(360, 300);

} // namespace

std::optional<int> TweenDialog::nextPose(const doc::Cel &cel, int frame)
{
    const doc::DrawingId shown = cel.timing().drawingAt(frame);
    if (shown == doc::kNoId)
        return std::nullopt;
    for (const doc::Exposure &e : cel.timing().exposures())
        if (e.start > frame && e.drawing != shown)
            return e.start;
    return std::nullopt;
}

TweenDialog::TweenDialog(document::DocumentController *document, QWidget *parent)
    : QDialog(parent)
    , m_document(document)
    , m_cel(document->currentCel())
{
    setWindowTitle(tr("Tween frames"));
    const doc::Cel *cel = m_document->document().cel(m_cel);
    const int current = m_document->currentFrame();
    const std::optional<int> next = cel ? nextPose(*cel, current) : std::nullopt;

    auto *celName = new QLabel(cel ? QString::fromStdString(cel->properties().name) : tr("(no cel)"), this);
    m_from = new QSpinBox(this);
    m_to = new QSpinBox(this);
    for (QSpinBox *box : {m_from, m_to})
        box->setRange(1, kMaxFrame);
    m_from->setValue(current + 1);
    m_to->setValue((next ? *next : current + 8) + 1);
    m_ease = new QComboBox(this);
    m_ease->addItem(tr("Linear"), int(document::TweenEase::Linear));
    m_ease->addItem(tr("Ease in"), int(document::TweenEase::EaseIn));
    m_ease->addItem(tr("Ease out"), int(document::TweenEase::EaseOut));
    m_ease->addItem(tr("Ease in and out"), int(document::TweenEase::EaseInOut));
    const int saved = QSettings().value(kEaseKey, int(document::TweenEase::Linear)).toInt();
    m_ease->setCurrentIndex(std::max(0, m_ease->findData(saved)));

    m_at = new QSlider(Qt::Horizontal, this);
    m_at->setRange(0, 100);
    m_at->setValue(50);
    m_at->setToolTip(tr("Where between the two poses the picture below is"));
    m_preview = new QLabel(this);
    m_preview->setMinimumSize(kPreviewSize);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameShape(QFrame::StyledPanel);
    m_note = new QLabel(this);
    m_note->setWordWrap(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_ok = buttons->addButton(tr("Make the frames"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &TweenDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow(tr("Cel:"), celName);
    form->addRow(tr("From frame:"), m_from);
    form->addRow(tr("To frame:"), m_to);
    form->addRow(tr("Easing:"), m_ease);
    form->addRow(tr("Preview at:"), m_at);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_preview, 1);
    layout->addWidget(m_note);
    layout->addWidget(buttons);

    connect(m_from, &QSpinBox::valueChanged, this, &TweenDialog::poseChanged);
    connect(m_to, &QSpinBox::valueChanged, this, &TweenDialog::poseChanged);
    connect(m_ease, &QComboBox::currentIndexChanged, this, [this] { showPreview(); });
    connect(m_at, &QSlider::valueChanged, this, [this] { showPreview(); });
    poseChanged();
}

int TweenDialog::from() const
{
    return m_from->value() - 1;
}

int TweenDialog::to() const
{
    return m_to->value() - 1;
}

document::TweenEase TweenDialog::ease() const
{
    return document::TweenEase(m_ease->currentData().toInt());
}

void TweenDialog::poseChanged()
{
    m_tween.reset();
    m_frames = 0;
    QString reason;
    const doc::Cel *cel = m_document->document().cel(m_cel);
    if (!cel) {
        reason = tr("No cel selected");
    } else if (to() - from() < 2) {
        reason = tr("There is no frame between the two poses");
    } else if (cel->timing().drawingAt(from()) == doc::kNoId || cel->timing().drawingAt(to()) == doc::kNoId) {
        reason = tr("Both ends of a tween need a drawing");
    } else if (cel->timing().drawingAt(from()) == cel->timing().drawingAt(to())) {
        reason = tr("The two frames show the same drawing");
    } else if (cel->properties().locked) {
        reason = tr("The cel is locked");
    } else {
        QApplication::setOverrideCursor(Qt::WaitCursor); // laying the two poses over each other takes a moment
        auto made = document::DrawingTween::make(*cel->drawing(cel->timing().drawingAt(from())),
                                                 *cel->drawing(cel->timing().drawingAt(to())), &reason);
        QApplication::restoreOverrideCursor();
        if (made) {
            m_tween = std::make_shared<const document::DrawingTween>(std::move(*made));
            m_frames = to() - from() - 1;
        }
    }
    m_ok->setEnabled(m_frames > 0);
    m_note->setText(m_frames > 0 ? tr("%n frame(s) will be made, replacing what they show now.", "", m_frames) : reason);
    showPreview();
}

void TweenDialog::showPreview()
{
    if (!m_tween) {
        m_preview->setPixmap(QPixmap());
        m_preview->setText(tr("Nothing to show"));
        return;
    }
    // One picture at a time, made apart: if the slider moves meanwhile, only the last place it was left at is made next.
    if (m_busy) {
        m_again = true;
        return;
    }
    startPreview();
}

void TweenDialog::startPreview()
{
    m_busy = true;
    m_again = false;
    const double t = document::easedTime(ease(), m_at->value() / 100.0);
    const std::shared_ptr<const document::DrawingTween> tween = m_tween;
    const QPointer<TweenDialog> self(this);
    QThreadPool::globalInstance()->start([tween, t, self] {
        const QImage picture = tween->preview(t);
        if (self)
            QMetaObject::invokeMethod(self.data(), [self, picture] {
                if (self)
                    self->previewReady(picture);
            }, Qt::QueuedConnection);
    });
}

void TweenDialog::previewReady(const QImage &picture)
{
    m_busy = false;
    if (m_tween) {
        QImage flat(picture.size(), QImage::Format_RGB32);
        flat.fill(Qt::white); // behind a paper that is see-through
        QPainter p(&flat);
        p.fillRect(flat.rect(), QColor::fromRgba(m_document->document().paperColor()));
        p.drawImage(0, 0, picture);
        p.end();
        m_preview->setPixmap(QPixmap::fromImage(flat.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    if (m_again)
        showPreview();
}

void TweenDialog::accept()
{
    QSettings().setValue(kEaseKey, m_ease->currentData().toInt());
    QDialog::accept();
}

} // namespace mothion::ui
