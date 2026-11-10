#include "ui/BlenderLiveLink.h"

#include "core/Parallel.h"

#include <QFileInfo>

#include <algorithm>
#include <map>

namespace mothion::ui {

BlenderLiveLink::BlenderLiveLink(document::DocumentController *document, io::BlenderLinkServer *server,
                                 io::BlenderFrameCache *cache, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_server(server)
    , m_cache(cache)
{
    connect(m_server, &io::BlenderLinkServer::sceneChanged, this, &BlenderLiveLink::onSceneChanged);
    connect(m_server, &io::BlenderLinkServer::frameReceived, this, &BlenderLiveLink::onFrame);
    connect(m_server, &io::BlenderLinkServer::requestFinished, this, &BlenderLiveLink::onFinished);
    // Another project opened: the cels are not there any more.
    connect(m_document, &document::DocumentController::documentReplaced, this, [this] {
        m_stopped = true;
        m_queue.clear();
        if (m_request)
            m_server->cancel(m_request);
    });
}

QImage BlenderLiveLink::pageSized(const QImage &image) const
{
    const doc::Document &d = m_document->document();
    if (image.width() == d.width() && image.height() == d.height())
        return image;
    return image.scaled(d.width(), d.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// From `lookingAt` to the end of the scene, then the frames before it: what is looked at comes first.
QList<int> BlenderLiveLink::order(int lookingAt) const
{
    const int last = m_first + m_count - 1;
    const int pivot = std::clamp(m_first + (lookingAt - m_startFrame), m_first, last);
    QList<int> frames;
    for (int f = pivot; f <= last; ++f)
        frames << f;
    for (int f = m_first; f < pivot; ++f)
        frames << f;
    return frames;
}

bool BlenderLiveLink::isConnected(doc::CelId cel) const
{
    const doc::Cel *c = m_document->document().cel(cel);
    if (!c || !c->blenderLink())
        return false;
    const doc::BlenderLink &link = *c->blenderLink();
    return m_server->sessionForKey(QString::fromStdString(link.path) + QLatin1Char('|') + QString::fromStdString(link.scene))
        .has_value();
}

bool BlenderLiveLink::import(int sessionId, int startFrame, int lookingAt)
{
    const auto session = m_server->session(sessionId);
    if (!session || isBusy())
        return false;
    if (session->frameCount() <= 0 || !session->viewport) {
        emit finished(false, session->viewport ? tr("The scene has no frames.") : tr("There is no 3D viewport in that Blender window."), 0, false);
        return true;
    }
    m_mode = Mode::Import;
    m_session = sessionId;
    m_key = session->key();
    m_name = QFileInfo(session->file).completeBaseName();
    if (m_name.isEmpty())
        m_name = session->scene;
    m_startFrame = std::max(0, startFrame);
    m_first = session->frameStart;
    m_count = session->frameCount();
    m_done = 0;
    m_lookingAt = lookingAt;
    m_stopped = false;
    m_restart = false;
    m_percent = m_quality.percent;
    m_link = doc::BlenderLink{session->file.toStdString(), session->scene.toStdString(), m_startFrame, m_percent};
    m_sequence = m_document->beginSequenceCel(m_name, m_startFrame, m_count, &m_link);
    if (!m_sequence) {
        m_mode = Mode::None;
        return false;
    }
    begin(*session, m_startFrame, lookingAt);
    return true;
}

// Asks the session for all the frames of the scene, in order.
void BlenderLiveLink::begin(const io::BlenderSession &session, int, int lookingAt)
{
    io::BlenderFrameRequest request;
    request.frames = order(lookingAt);
    request.width = std::max(1, m_document->document().width() * m_percent / 100);
    request.height = std::max(1, m_document->document().height() * m_percent / 100);
    request.shading = m_quality.shading;
    request.compositor = m_quality.compositor;
    m_request = m_server->request(session.id, request);
    emit status(m_mode == Mode::Update ? tr("Blender: updating “%1”: 0 of %2 frames").arg(m_name).arg(m_count)
                                       : tr("Blender: 0 of %1 frames").arg(m_count));
    if (m_request == 0) { // the Blender went away between the two
        m_stopped = true;
        onFinished(0, false, false, tr("Blender was closed."));
    }
}

bool BlenderLiveLink::update(doc::CelId cel)
{
    if (!isConnected(cel))
        return false;
    if (m_mode == Mode::Update && m_cel == cel) {
        // Changed again while it was being brought up to date: what was made so far is kept, and it starts over.
        m_restart = true;
        m_server->cancel(m_request);
        return true;
    }
    if (std::find(m_queue.begin(), m_queue.end(), cel) == m_queue.end())
        m_queue.push_back(cel);
    if (m_mode == Mode::None)
        startNext();
    return true;
}

void BlenderLiveLink::startNext()
{
    while (m_mode == Mode::None && !m_queue.empty()) {
        const doc::CelId cel = m_queue.front();
        m_queue.pop_front();
        if (!isConnected(cel))
            continue;
        const doc::Cel *c = m_document->document().cel(cel);
        const doc::BlenderLink &link = *c->blenderLink();
        const auto session = m_server->sessionForKey(QString::fromStdString(link.path) + QLatin1Char('|') + QString::fromStdString(link.scene));
        if (!session || session->frameCount() <= 0 || !session->viewport)
            continue;
        m_mode = Mode::Update;
        m_cel = cel;
        m_session = session->id;
        m_key = session->key();
        m_name = QString::fromStdString(c->properties().name);
        m_startFrame = link.startFrame;
        m_first = session->frameStart;
        m_count = session->frameCount();
        m_done = 0;
        m_lookingAt = m_document->currentFrame();
        m_early = false;
        m_earlyIndex = -1;
        m_stopped = false;
        m_restart = false;
        m_percent = link.percent;
        m_update = m_document->beginSequenceUpdate(cel, link.startFrame);
        if (!m_update) {
            m_mode = Mode::None;
            continue;
        }
        m_cache->invalidate(m_key); // what is kept is old now; the frames that come are the new ones
        begin(*session, m_startFrame, m_lookingAt);
    }
}

void BlenderLiveLink::onSceneChanged(int sessionId)
{
    if (!m_automatic)
        return;
    const auto session = m_server->session(sessionId);
    if (!session)
        return;
    for (const auto &[cel, link] : m_document->blenderLinks())
        if (QString::fromStdString(link.path) + QLatin1Char('|') + QString::fromStdString(link.scene) == session->key())
            update(cel);
}

void BlenderLiveLink::onFrame(int request, int frame, const QImage &image)
{
    if (request != m_request || m_stopped)
        return;
    const int index = frame - m_first;
    m_cache->put(m_key, frame, image);
    if (m_mode == Mode::Import) {
        if (!m_sequence)
            return;
        if (!m_document->fillSequenceFrame(*m_sequence, index, pageSized(image))) {
            m_stopped = true; // the cel is gone (undone): nothing to fill any more
            m_server->cancel(m_request);
            return;
        }
        emit status(tr("Blender: %1 of %2 frames").arg(++m_done).arg(m_count));
    } else if (m_mode == Mode::Update && m_update) {
        ++m_done;
        if (!m_early && !(m_playing && m_playing())) {
            // The picture looked at, the first to come: in at once, so the change is seen as soon as it can be.
            m_early = true;
            m_earlyIndex = index;
            if (!m_document->applySequenceFrames(*m_update, {{index, pageSized(image)}})) {
                m_stopped = true;
                m_server->cancel(m_request);
                return;
            }
        }
        emit status(tr("Blender: updating “%1”: %2 of %3 frames").arg(m_name).arg(m_done).arg(m_count));
    }
}

void BlenderLiveLink::onFinished(int request, bool ok, bool cancelled, const QString &error)
{
    if (request != m_request || m_mode == Mode::None)
        return;
    const Mode mode = m_mode;
    int frames = m_done;
    if (mode == Mode::Import) {
        if (m_sequence && m_done > 0 && !m_stopped)
            m_document->finishSequence(*m_sequence); // what was made is one more undo step
    } else if (m_update) {
        if (ok && !m_stopped) {
            // All the new frames, together: scaled on all the cores, some at a time (a frame is a few megabytes), and put in.
            std::vector<int> waiting;
            for (int f : m_cache->frames(m_key))
                if (m_cache->isFresh(m_key, f) && f - m_first != m_earlyIndex)
                    waiting.push_back(f);
            constexpr int kChunk = 8;
            for (std::size_t at = 0; at < waiting.size() && !m_stopped; at += kChunk) {
                const int n = int(std::min<std::size_t>(kChunk, waiting.size() - at));
                std::vector<QImage> images;
                images.resize(std::size_t(n));
                std::vector<QImage> sources;
                for (int i = 0; i < n; ++i)
                    sources.push_back(m_cache->get(m_key, waiting[at + std::size_t(i)]));
                parallelFor(n, [&](int i) { images[std::size_t(i)] = pageSized(sources[std::size_t(i)]); });
                std::map<int, QImage> chunk;
                for (int i = 0; i < n; ++i)
                    chunk[waiting[at + std::size_t(i)] - m_first] = std::move(images[std::size_t(i)]);
                if (!m_document->applySequenceFrames(*m_update, chunk))
                    m_stopped = true;
            }
        }
        // What was put in (all of it, or only the picture looked at) is one undo step.
        m_document->finishSequenceUpdate(*m_update);
        if (!ok)
            frames = m_early ? 1 : 0;
    }
    m_update.reset();
    m_sequence.reset();
    m_request = 0;
    m_mode = Mode::None;
    const bool restart = mode == Mode::Update && m_restart && !m_stopped;
    m_restart = false;
    if (restart) { // changed again: from the start (the cel goes first in the queue)
        m_queue.push_front(m_cel);
    } else {
        emit finished(ok, cancelled ? QString() : error, frames, mode == Mode::Update);
    }
    startNext();
}

void BlenderLiveLink::cancel()
{
    m_queue.clear();
    m_restart = false;
    if (m_request)
        m_server->cancel(m_request);
}

} // namespace mothion::ui
