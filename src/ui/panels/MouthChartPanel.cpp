#include "ui/panels/MouthChartPanel.h"

#include "canvas/FrameRenderer.h"
#include "core/document/LipSync.h"
#include "document/DocumentController.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

constexpr QSize kPicture(84, 64);

QString shapeHint(char shape)
{
    switch (shape) {
    case 'A': return MouthChartPanel::tr("M B P");
    case 'B': return MouthChartPanel::tr("K S T, EE");
    case 'C': return MouthChartPanel::tr("E");
    case 'D': return MouthChartPanel::tr("A");
    case 'E': return MouthChartPanel::tr("O");
    case 'F': return MouthChartPanel::tr("U W");
    case 'G': return MouthChartPanel::tr("F V");
    case 'H': return MouthChartPanel::tr("L");
    default: return MouthChartPanel::tr("rest");
    }
}

} // namespace

MouthChartPanel::MouthChartPanel(document::DocumentController *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
{
    m_pages = new QStackedLayout(this);

    // A cel that is no mouth chart: what this is, and a button to make it one.
    auto *none = new QWidget(this);
    auto *noneLayout = new QVBoxLayout(none);
    m_noChart = new QLabel(none);
    m_noChart->setWordWrap(true);
    m_noChart->setForegroundRole(QPalette::PlaceholderText);
    m_makeChart = new QPushButton(none);
    m_makeChart->setObjectName(QStringLiteral("makeMouthChart"));
    connect(m_makeChart, &QPushButton::clicked, this,
            [this] { m_document->setMouthChart(m_document->currentCel(), true); });
    noneLayout->addWidget(m_noChart);
    noneLayout->addWidget(m_makeChart);
    noneLayout->addStretch(1);
    m_pages->addWidget(none);

    // The mouths, three by three.
    auto *chart = new QWidget(this);
    auto *chartLayout = new QVBoxLayout(chart);
    auto *grid = new QGridLayout;
    grid->setSpacing(4);
    for (std::size_t i = 0; i < doc::kMouthShapes.size(); ++i) {
        const char shape = doc::kMouthShapes[i];
        auto *b = new QToolButton(chart);
        b->setObjectName(QStringLiteral("mouthChart%1").arg(QChar(shape)));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(kPicture);
        b->setText(QStringLiteral("%1  %2").arg(QChar(shape)).arg(shapeHint(shape)));
        b->setToolTip(tr("Draw mouth %1 (%2): click to draw it on the canvas, click again to go back to the timeline")
                          .arg(QChar(shape))
                          .arg(shapeHint(shape)));
        connect(b, &QToolButton::clicked, this, [this, shape](bool on) {
            const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
            const doc::DrawingId drawing = cel ? doc::mouthDrawing(*cel, shape) : doc::kNoId;
            if (on && drawing != doc::kNoId)
                m_document->editMouthDrawing(drawing);
            else
                m_document->stopEditingMouth();
            sync();
        });
        grid->addWidget(b, int(i / 3), int(i % 3));
        m_buttons[i] = b;
    }
    chartLayout->addLayout(grid);
    m_hint = new QLabel(chart);
    m_hint->setWordWrap(true);
    m_hint->setForegroundRole(QPalette::PlaceholderText);
    chartLayout->addWidget(m_hint);
    chartLayout->addStretch(1);
    m_pages->addWidget(chart);

    m_pictureTimer = new QTimer(this);
    m_pictureTimer->setSingleShot(true);
    m_pictureTimer->setInterval(150);
    connect(m_pictureTimer, &QTimer::timeout, this, &MouthChartPanel::refreshPictures);

    using document::DocumentController;
    connect(m_document, &DocumentController::currentCelChanged, this, &MouthChartPanel::sync);
    connect(m_document, &DocumentController::structureChanged, this, &MouthChartPanel::sync);
    connect(m_document, &DocumentController::documentReplaced, this, &MouthChartPanel::sync);
    connect(m_document, &DocumentController::mouthEditChanged, this, &MouthChartPanel::sync);
    connect(m_document, &DocumentController::pixelsChanged, this, [this](doc::CelId cel) {
        if (cel == m_document->currentCel() && m_pages->currentIndex() == 1 && !m_pictureTimer->isActive())
            m_pictureTimer->start();
    });
    sync();
}

QToolButton *MouthChartPanel::shapeButton(char shape) const
{
    for (std::size_t i = 0; i < doc::kMouthShapes.size(); ++i)
        if (doc::kMouthShapes[i] == shape)
            return m_buttons[i];
    return nullptr;
}

void MouthChartPanel::sync()
{
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (!cel || !cel->properties().mouthChart) {
        m_pages->setCurrentIndex(0);
        const bool can = cel && !cel->properties().trajectory;
        m_noChart->setText(tr("The mouths of a character for the lip sync: a cel with a frame for each mouth shape "
                              "(A to H, X). The current cel is not a mouth chart."));
        m_makeChart->setText(cel ? tr("Make “%1” a mouth chart").arg(QString::fromStdString(cel->properties().name))
                                 : tr("Make the current cel a mouth chart"));
        m_makeChart->setEnabled(can);
        return;
    }
    m_pages->setCurrentIndex(1);
    const doc::DrawingId edited = m_document->mouthDrawingEdited();
    for (std::size_t i = 0; i < doc::kMouthShapes.size(); ++i) {
        const doc::DrawingId drawing = doc::mouthDrawing(*cel, doc::kMouthShapes[i]);
        m_buttons[i]->setEnabled(drawing != doc::kNoId);
        m_buttons[i]->setChecked(drawing != doc::kNoId && drawing == edited);
    }
    m_hint->setText(edited != doc::kNoId
                        ? tr("Drawing a mouth on the canvas: click it again (or choose another frame) to go back to "
                             "the timeline.")
                        : tr("Click a mouth to draw it. The Timeline's mouth bar (or the letters, in the timeline) "
                             "puts them frame by frame."));
    refreshPictures();
}

void MouthChartPanel::refreshPictures()
{
    const doc::Cel *cel = m_document->document().cel(m_document->currentCel());
    if (!cel || !cel->properties().mouthChart)
        return;
    // All framed alike: the area that holds every mouth, so they are seen at the same size and place.
    QRect area;
    for (const char shape : doc::kMouthShapes)
        if (const doc::Drawing *d = cel->drawing(doc::mouthDrawing(*cel, shape)))
            area |= canvas::drawingBounds(*d, false);
    if (!area.isEmpty()) {
        const int pad = std::max(area.width(), area.height()) / 10 + 4;
        area.adjust(-pad, -pad, pad, pad);
    }
    for (std::size_t i = 0; i < doc::kMouthShapes.size(); ++i) {
        QPixmap picture(kPicture);
        picture.fill(Qt::white);
        const doc::Drawing *d = cel->drawing(doc::mouthDrawing(*cel, doc::kMouthShapes[i]));
        if (d && !area.isEmpty()) {
            const QImage image = canvas::composeDrawing(*d, area, false);
            const QImage fitted = image.scaled(kPicture, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPainter p(&picture);
            p.drawImage(QPoint((kPicture.width() - fitted.width()) / 2, (kPicture.height() - fitted.height()) / 2),
                        fitted);
        }
        {
            QPainter p(&picture);
            p.setPen(QColor(0, 0, 0, 60));
            p.drawRect(picture.rect().adjusted(0, 0, -1, -1));
        }
        m_buttons[i]->setIcon(QIcon(picture));
    }
}

} // namespace mothion::ui
