#include "ui/AdvancedToolSettings.h"

#include "ui/panels/SliderField.h"

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

namespace mothion::ui {

using tools::ToolState;

namespace {

constexpr char kHelpTitle[] = "helpTitle";
constexpr char kHelpText[] = "helpText";

// A page of settings: a top-aligned column.
QVBoxLayout *pageLayout(QStackedWidget *pages, QWidget **page)
{
    *page = new QWidget(pages);
    auto *layout = new QVBoxLayout(*page);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);
    pages->addWidget(*page);
    return layout;
}

// Options that belong to the one above: indented under it.
QVBoxLayout *subOptions(QVBoxLayout *page)
{
    auto *layout = new QVBoxLayout;
    layout->setContentsMargins(22, 0, 0, 0);
    layout->setSpacing(4);
    page->addLayout(layout);
    return layout;
}

} // namespace

AdvancedToolSettings::AdvancedToolSettings(ToolState *state, QWidget *parent)
    : QDialog(parent, Qt::Tool)
    , m_state(state)
    , m_brushName(new QLabel(this))
    , m_categories(new QListWidget(this))
    , m_pages(new QStackedWidget(this))
    , m_helpTitle(new QLabel(this))
    , m_help(new QLabel(this))
{
    setWindowTitle(tr("Advanced Tool Settings"));
    m_brushName->setObjectName(QStringLiteral("PanelTitle"));

    // Correction
    QWidget *correctionPage = nullptr;
    QVBoxLayout *correction = pageLayout(m_pages, &correctionPage);
    m_sharpCorners = new QCheckBox(tr("Sharp angles"), correctionPage);
    setHelp(m_sharpCorners, tr("When this is on, angles in the line will be made sharper."));
    correction->addWidget(m_sharpCorners);
    m_stabilization = new SliderField(tr("Stabilization"), 0, 95, QStringLiteral(" %"), correctionPage);
    setHelp(m_stabilization, tr("Smooths the pen's wobble while drawing. Higher values give smoother lines, "
                                "but the line follows the pen with more delay."));
    correction->addWidget(m_stabilization);

    m_postCorrection = new QCheckBox(tr("Post correction"), correctionPage);
    setHelp(m_postCorrection, tr("When the pen lifts, the line is simplified into fewer points, smoothly joined. "
                                 "The level sets how much it is simplified."));
    correction->addWidget(m_postCorrection);
    QVBoxLayout *post = subOptions(correction);
    m_postLevel = new SliderField(tr("Level"), 1, 100, QString(), correctionPage);
    m_postLevel->setValue(10);
    setHelp(m_postLevel, tr("How much the line is simplified when the pen lifts."));
    post->addWidget(m_postLevel);
    m_bySpeed = new QCheckBox(tr("Adjust by speed"), correctionPage);
    setHelp(m_bySpeed, tr("The correction follows the pen's speed: lines drawn quickly are simplified more "
                          "and their sharp turns become corners, slow lines stay smooth. "
                          "Works with Bezier curve."));
    post->addWidget(m_bySpeed);
    m_bezier = new QCheckBox(tr("Bezier curve"), correctionPage);
    setHelp(m_bezier, tr("The corrected line is made of quadratic curves, and the corners found on it stay "
                         "sharp."));
    post->addWidget(m_bezier);
    correction->addStretch(1);

    // Starting and ending
    QWidget *endsPage = nullptr;
    QVBoxLayout *ends = pageLayout(m_pages, &endsPage);
    m_correctStart = new QCheckBox(tr("Correct the start after the stroke"), endsPage);
    setHelp(m_correctStart, tr("On: the brush's start correction thins the start once the pen lifts, as in "
                               "Clip Studio. Off: the start thins while drawing."));
    ends->addWidget(m_correctStart);
    ends->addStretch(1);

    m_categories->addItems({tr("Correction"), tr("Starting and ending")});
    m_categories->setFixedWidth(m_categories->sizeHintForColumn(0) + 24);
    connect(m_categories, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_categories->setCurrentRow(0);

    auto *helpBox = new QFrame(this);
    helpBox->setFrameShape(QFrame::StyledPanel);
    auto *helpLayout = new QVBoxLayout(helpBox);
    m_helpTitle->setStyleSheet(QStringLiteral("font-weight: bold"));
    m_help->setWordWrap(true);
    m_help->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_help->setMinimumHeight(m_help->fontMetrics().lineSpacing() * 3);
    helpLayout->addWidget(m_helpTitle);
    helpLayout->addWidget(m_help, 1);

    auto *body = new QHBoxLayout;
    body->addWidget(m_categories);
    body->addWidget(m_pages, 1);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_brushName);
    layout->addLayout(body, 1);
    layout->addWidget(helpBox);
    resize(520, 420);

    connect(m_sharpCorners, &QCheckBox::toggled, m_state, &ToolState::setSharpCorners);
    connect(m_stabilization, &SliderField::valueEdited, this, [this](int v) { m_state->setSmoothing(v / 100.0); });
    connect(m_correctStart, &QCheckBox::toggled, m_state, &ToolState::setCorrectStartAfterStroke);
    connect(m_postCorrection, &QCheckBox::toggled, this, [this](bool on) {
        const int level = m_postLevel->value();
        editStyle([on, level](brush::BrushStyle &s) { s.revision = on ? level : 0; });
    });
    connect(m_postLevel, &SliderField::valueEdited, this,
            [this](int v) { editStyle([v](brush::BrushStyle &s) { s.revision = v; }); });
    connect(m_bySpeed, &QCheckBox::toggled, this,
            [this](bool on) { editStyle([on](brush::BrushStyle &s) { s.revisionBySpeed = on; }); });
    connect(m_bezier, &QCheckBox::toggled, this,
            [this](bool on) { editStyle([on](brush::BrushStyle &s) { s.revisionCurves = on; }); });

    connect(m_state, &ToolState::activeToolChanged, this, &AdvancedToolSettings::syncFromState);
    connect(m_state, &ToolState::toolSettingsChanged, this, &AdvancedToolSettings::syncFromState);
    connect(m_state, &ToolState::smoothingChanged, this, &AdvancedToolSettings::syncFromState);
    connect(m_state, &ToolState::sharpCornersChanged, this, &AdvancedToolSettings::syncFromState);
    connect(m_state, &ToolState::correctStartAfterStrokeChanged, this, &AdvancedToolSettings::syncFromState);
    syncFromState();
    m_helpTitle->setText(m_sharpCorners->text());
    m_help->setText(m_sharpCorners->property(kHelpText).toString());
}

template <typename Change> void AdvancedToolSettings::editStyle(Change change)
{
    const tools::ToolId tool = m_state->activeTool();
    if (!ToolState::isPaintTool(tool))
        return;
    auto edited = std::make_shared<tools::BrushDefinition>(*m_state->brush(tool));
    change(edited->style);
    m_state->setBrush(tool, std::move(edited));
}

void AdvancedToolSettings::setHelp(QWidget *widget, const QString &help)
{
    QString title = widget->property("text").toString();
    if (title.isEmpty())
        title = widget->findChild<QLabel *>() ? widget->findChild<QLabel *>()->text() : QString();
    widget->setProperty(kHelpTitle, title);
    widget->setProperty(kHelpText, help);
    widget->installEventFilter(this);
}

bool AdvancedToolSettings::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Enter && watched->property(kHelpText).isValid()) {
        m_helpTitle->setText(watched->property(kHelpTitle).toString());
        m_help->setText(watched->property(kHelpText).toString());
    }
    return QDialog::eventFilter(watched, event);
}

void AdvancedToolSettings::syncFromState()
{
    const tools::ToolId tool = m_state->activeTool();
    const bool paint = ToolState::isPaintTool(tool);
    const brush::BrushStyle &style = m_state->brush(tool)->style;
    m_brushName->setText(paint ? m_state->brush(tool)->name : ToolState::displayName(tool));
    if (m_brushName->text().isEmpty())
        m_brushName->setText(ToolState::displayName(tool));

    const QSignalBlocker b1(m_sharpCorners), b2(m_postCorrection), b3(m_bySpeed), b4(m_bezier),
        b5(m_correctStart);
    m_sharpCorners->setChecked(m_state->sharpCorners());
    m_stabilization->setValue(int(std::lround(m_state->smoothing() * 100)));
    m_correctStart->setChecked(m_state->correctsStartAfterStroke());

    // The post correction belongs to the brush: only a paint tool has one.
    const bool post = paint && style.revision > 0;
    m_postCorrection->setEnabled(paint);
    m_postCorrection->setChecked(post);
    if (post)
        m_postLevel->setValue(style.revision);
    m_bySpeed->setChecked(style.revisionBySpeed);
    m_bezier->setChecked(style.revisionCurves);
    m_postLevel->setEnabled(post);
    m_bezier->setEnabled(post);
    m_bySpeed->setEnabled(post && style.revisionCurves); // only the curve correction follows the speed
}

} // namespace mothion::ui
