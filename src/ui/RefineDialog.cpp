#include "ui/RefineDialog.h"

#include "ai/AiSettings.h"
#include "ai/ComfyClient.h"
#include "ai/ServerProcess.h"
#include "document/DocumentController.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace mothion::ui {

namespace {

const QString kPromptKey = QStringLiteral("ai/prompt");
const QString kNegativeKey = QStringLiteral("ai/negative");
constexpr QSize kPreview{440, 280};

} // namespace

RefineDialog::RefineDialog(document::DocumentController *controller, ai::ServerProcess *server, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_client(new ai::ComfyClient(this))
    , m_server(server)
{
    setObjectName(QStringLiteral("RefineDialog"));
    setWindowTitle(tr("Refine with AI"));

    m_serverState = new QLabel(this);
    m_serverState->setObjectName(QStringLiteral("serverState"));
    m_serverState->setWordWrap(true);
    m_start = new QPushButton(tr("Start"), this);
    m_start->setObjectName(QStringLiteral("startServer"));
    m_stopServer = new QPushButton(tr("Stop"), this);
    m_stopServer->setObjectName(QStringLiteral("stopServer"));
    auto *serverRow = new QHBoxLayout;
    serverRow->addWidget(new QLabel(tr("ComfyUI:"), this));
    serverRow->addWidget(m_serverState, 1);
    serverRow->addWidget(m_start);
    serverRow->addWidget(m_stopServer);
    connect(m_start, &QPushButton::clicked, m_server, &ai::ServerProcess::start);
    connect(m_stopServer, &QPushButton::clicked, m_server, &ai::ServerProcess::stop);
    connect(m_server, &ai::ServerProcess::changed, this, &RefineDialog::showServer);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("preview"));
    m_preview->setFixedSize(kPreview);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setFrameShape(QFrame::StyledPanel);
    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("info"));
    m_info->setWordWrap(true);

    QSettings settings;
    m_prompt = new QPlainTextEdit(settings.value(kPromptKey).toString(), this);
    m_prompt->setObjectName(QStringLiteral("prompt"));
    m_prompt->setPlaceholderText(tr("What it is, as tags: 1girl, pleated skirt, from below, crosshatching…"));
    m_prompt->setFixedHeight(64);
    m_negative = new QPlainTextEdit(settings.value(kNegativeKey).toString(), this);
    m_negative->setObjectName(QStringLiteral("negative"));
    m_negative->setPlaceholderText(tr("What to avoid"));
    m_negative->setFixedHeight(48);
    auto *hint = new QLabel(tr("The standing prompt and negative of Settings > AI are added to these."), this);
    hint->setWordWrap(true);
    hint->setEnabled(false);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("status"));
    m_status->setWordWrap(true);
    m_bar = new QProgressBar(this);
    m_bar->setObjectName(QStringLiteral("bar"));
    m_bar->setRange(0, 0); // busy: no way to know how far it is
    m_bar->setTextVisible(false);
    m_bar->hide();

    m_generate = new QPushButton(tr("Generate"), this);
    m_generate->setObjectName(QStringLiteral("generate"));
    m_generate->setDefault(true);
    m_stop = new QPushButton(tr("Stop"), this);
    m_stop->setObjectName(QStringLiteral("stop"));
    m_stop->hide();
    auto *buttons = new QDialogButtonBox(this);
    buttons->addButton(m_generate, QDialogButtonBox::AcceptRole);
    buttons->addButton(m_stop, QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    m_stop->hide(); // (after the box has taken it: adding a button shows it)

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(serverRow);
    layout->addWidget(m_preview, 0, Qt::AlignHCenter);
    layout->addWidget(m_info);
    auto *form = new QFormLayout;
    form->addRow(tr("Prompt"), m_prompt);
    form->addRow(tr("Negative"), m_negative);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(m_status);
    layout->addWidget(m_bar);
    layout->addWidget(buttons);

    connect(m_generate, &QPushButton::clicked, this, &RefineDialog::generate);
    connect(m_stop, &QPushButton::clicked, this, &RefineDialog::stop);
    connect(m_client, &ai::ComfyClient::status, m_status, &QLabel::setText);
    connect(m_client, &ai::ComfyClient::finished, this, &RefineDialog::finish);
    connect(m_client, &ai::ComfyClient::failed, this, &RefineDialog::failWith);

    // The picture follows what is chosen, selected and drawn (a moment after the last change: strokes change a lot).
    m_later.setSingleShot(true);
    m_later.setInterval(150);
    connect(&m_later, &QTimer::timeout, this, &RefineDialog::refreshReference);
    auto soon = [this] { m_later.start(); };
    using document::DocumentController;
    connect(m_controller, &DocumentController::layerSelectionChanged, this, soon);
    connect(m_controller, &DocumentController::selectionChanged, this, soon);
    connect(m_controller, &DocumentController::currentFrameChanged, this, soon);
    connect(m_controller, &DocumentController::currentCelChanged, this, soon);
    connect(m_controller, &DocumentController::currentLayerChanged, this, soon);
    connect(m_controller, &DocumentController::layerChosenChanged, this, soon);
    connect(m_controller, &DocumentController::structureChanged, this, soon);
    connect(m_controller, &DocumentController::pixelsChanged, this, soon);
    connect(m_controller, &DocumentController::documentReplaced, this, soon);

    showServer();
    refreshReference();
}

void RefineDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    m_server->setWatching(true); // the state is looked at while the window is there
    showServer();
}

void RefineDialog::hideEvent(QHideEvent *event)
{
    QDialog::hideEvent(event);
    m_server->setWatching(false);
}

void RefineDialog::showServer()
{
    using State = ai::ServerProcess::State;
    QString text;
    switch (m_server->state()) {
    case State::Stopped: text = tr("stopped"); break;
    case State::Starting: text = tr("starting… (loading takes a moment)"); break;
    case State::Running: text = m_server->startedHere() ? tr("running") : tr("running (started elsewhere)"); break;
    }
    if (!m_server->problem().isEmpty())
        text += QStringLiteral("\n") + m_server->problem();
    m_serverState->setText(text);
    QString why;
    const bool canStart = m_server->canStart(&why);
    m_start->setEnabled(m_server->state() == State::Stopped && canStart);
    m_start->setToolTip(canStart ? QString() : why);
    m_stopServer->setEnabled(m_server->startedHere());
    m_stopServer->setToolTip(m_server->state() == State::Running && !m_server->startedHere()
                                 ? tr("It was started outside Mothion: stop it there.") : QString());
}

void RefineDialog::refreshReference()
{
    m_later.stop();
    if (m_generating)
        return; // the picture that went stays on show until the result is in
    m_reference = ai::makeReference(*m_controller);
    showReference();
}

void RefineDialog::showReference()
{
    const ai::Reference &shown = m_generating ? m_job : m_reference;
    if (shown.valid()) {
        m_preview->setPixmap(QPixmap::fromImage(shown.image).scaled(kPreview - QSize(4, 4), Qt::KeepAspectRatio,
                                                                      Qt::SmoothTransformation));
        const QString size = QStringLiteral("%1 × %2 px").arg(shown.image.width()).arg(shown.image.height());
        const QString layers = shown.layers == 1 ? tr("1 layer") : tr("%1 layers").arg(shown.layers);
        m_info->setText(tr("%1 of frame %2, %3").arg(layers).arg(shown.frame + 1).arg(size)
                        + (shown.selection.isEmpty() ? QString() : tr(" (the selection)")));
    } else {
        m_preview->setPixmap({});
        m_preview->setText(tr("No picture"));
        m_info->setText(shown.problem);
    }
    m_generate->setEnabled(!m_generating && m_reference.valid());
}

void RefineDialog::setBusy(bool busy)
{
    m_generating = busy;
    m_bar->setVisible(busy);
    m_stop->setVisible(busy);
    m_generate->setVisible(!busy);
    m_prompt->setEnabled(!busy);
    m_negative->setEnabled(!busy);
}

void RefineDialog::generate()
{
    if (m_generating || !m_reference.valid())
        return;
    QSettings settings;
    settings.setValue(kPromptKey, m_prompt->toPlainText());
    settings.setValue(kNegativeKey, m_negative->toPlainText());
    m_client->setServer(QUrl(ai::serverUrl()));
    m_client->setWorkflowPath(ai::workflowPath());
    m_job = m_reference;
    setBusy(true);
    m_status->clear();
    m_client->generate(m_job.image, ai::combinedPrompt(ai::systemPrompt(), m_prompt->toPlainText()),
                       ai::combinedPrompt(ai::systemNegative(), m_negative->toPlainText()));
    // (If it ended at once, the workflow could not be read: failed() has come already.)
}

void RefineDialog::stop()
{
    m_client->cancel();
    setBusy(false);
    m_status->setText(tr("Stopped."));
    refreshReference();
}

void RefineDialog::finish(const QImage &result)
{
    QImage picture = result;
    if (picture.size() != m_job.image.size()) // the workflow works at its own size
        picture = picture.scaled(m_job.image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QString why;
    const bool placed = m_controller->addImageLayerOnTop(picture, m_job.origin, m_job.selection, m_job.frame,
                                                         tr("AI refine"), &why);
    setBusy(false);
    m_status->setText(placed ? tr("Done: the result is a new layer on top of frame %1.").arg(m_job.frame + 1) : why);
    refreshReference();
}

void RefineDialog::failWith(const QString &message)
{
    setBusy(false);
    m_status->setText(message);
    refreshReference();
}

} // namespace mothion::ui
