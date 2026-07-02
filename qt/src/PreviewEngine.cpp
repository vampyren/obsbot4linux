#include "PreviewEngine.h"
#include "PreviewFormats.h"

#include <QVideoFrameFormat>

#include <algorithm>
#include <cmath>

namespace {
// A UVC format entry matches a requested (w, h, fps) when the resolution is
// exact and the requested rate falls inside the entry's advertised range
// (v4l2 discrete rates surface as min == max, so this is an exact match too).
bool formatMatches(const QCameraFormat &f, int w, int h, int fps) {
    return f.resolution().width() == w && f.resolution().height() == h
           && f.minFrameRate() <= fps + 0.5 && f.maxFrameRate() >= fps - 0.5;
}
} // namespace

PreviewEngine::PreviewEngine(QObject *parent) : QObject(parent) {
    connect(&m_mediaDevices, &QMediaDevices::videoInputsChanged,
            this, &PreviewEngine::refreshDevice);
    refreshDevice();
}

PreviewEngine::~PreviewEngine() {
    stop();
}

QString PreviewEngine::unavailableReason() const {
    if (available()) return QString();
    return QStringLiteral("No OBSBOT video device found — preview needs the camera's UVC node.");
}

void PreviewEngine::setVideoSink(QVideoSink *sink) {
    if (sink == m_sink) return;
    m_sink = sink;
    m_session.setVideoSink(m_sink);
    emit videoSinkChanged();
}

void PreviewEngine::refreshDevice() {
    // Pick the OBSBOT capture node by name. The Tiny 3 enumerates as
    // "OBSBOT Tiny 3: OBSBOT Tiny 3 St…" and may expose more than one node
    // (/dev/video0 capture + /dev/video1 metadata) — require MJPG formats so we
    // land on the real capture node, never a metadata one.
    const bool wasAvailable = available();
    QCameraDevice found;
    const auto inputs = QMediaDevices::videoInputs();
    for (const QCameraDevice &d : inputs) {
        if (!d.description().contains(QLatin1String("OBSBOT"), Qt::CaseInsensitive))
            continue;
        const auto formats = d.videoFormats();
        const bool hasJpeg = std::any_of(formats.cbegin(), formats.cend(),
            [](const QCameraFormat &f) {
                return f.pixelFormat() == QVideoFrameFormat::Format_Jpeg;
            });
        if (hasJpeg) { found = d; break; }
        if (found.isNull()) found = d;   // keep as fallback if no node has MJPG
    }

    if (found.id() == m_device.id() && !found.isNull()) return;   // same node, nothing to do

    // Gone OR re-enumerated under a new id (replug can shift /dev/videoN while
    // the old node lingers): either way the camera we're streaming from no
    // longer exists — stop honestly rather than leave a frozen last frame
    // "live" until the backend happens to error out.
    const bool lostWhileActive = m_active;
    m_device = found;

    if (lostWhileActive) {
        stop();
        emit logLine("err", QStringLiteral("preview: video device lost/changed — stopped"));
    }
    if (available() != wasAvailable || !found.isNull())
        emit availabilityChanged();
    if (available() && !wasAvailable)
        emit logLine("sys", QStringLiteral("preview: video device found (%1)").arg(m_device.description()));
}

QCameraFormat PreviewEngine::pickFormat() {
    const int ri = (m_resIndex >= 0 && m_resIndex < kPreviewResCount) ? m_resIndex : 0;
    const PreviewRes &want = kPreviewRes[ri];

    // Prefer a DISCRETE entry whose rate IS the wanted fps (min == max == fps).
    // A range entry (min..max) merely *containing* the wanted fps is second
    // choice — the backend streams such a format at its max rate, which is how
    // "1080p30" once came out as 1080p120 on hardware (¼ the exposure time →
    // washed-out colors and visibly worse frames than ffplay's honest 30).
    QCameraFormat exact;      // min == max == wanted fps, wanted resolution
    QCameraFormat containing; // range spanning wanted fps, wanted resolution
    QCameraFormat bestJpeg;   // last resort: highest-resolution MJPG mode
    const auto formats = m_device.videoFormats();
    for (const QCameraFormat &f : formats) {
        if (f.pixelFormat() != QVideoFrameFormat::Format_Jpeg) continue;   // MJPG only
        if (f.resolution().width() == want.w && f.resolution().height() == want.h) {
            const bool minHit = std::abs(f.minFrameRate() - want.fps) <= 0.5;
            const bool maxHit = std::abs(f.maxFrameRate() - want.fps) <= 0.5;
            if (minHit && maxHit) { exact = f; break; }
            if (formatMatches(f, want.w, want.h, want.fps)
                && (containing.isNull()
                    || f.maxFrameRate() < containing.maxFrameRate()))   // least overshoot
                containing = f;
        }
        if (bestJpeg.isNull()
            || f.resolution().width() * f.resolution().height()
                   > bestJpeg.resolution().width() * bestJpeg.resolution().height())
            bestJpeg = f;
    }
    if (!exact.isNull())
        return exact;
    if (!containing.isNull()) {
        emit logLine("warn",
            QStringLiteral("preview: no discrete %1 mode — using a %2–%3 fps range format "
                           "(the backend may run it above %4 fps)")
                .arg(QLatin1String(want.label))
                .arg(containing.minFrameRate(), 0, 'f', 0)
                .arg(containing.maxFrameRate(), 0, 'f', 0)
                .arg(want.fps));
        return containing;
    }
    if (!bestJpeg.isNull())
        emit logLine("warn",
            QStringLiteral("preview: %1 not advertised by the device — falling back to %2x%3")
                .arg(QLatin1String(want.label))
                .arg(bestJpeg.resolution().width()).arg(bestJpeg.resolution().height()));
    return bestJpeg;   // may be null → QCamera picks its own default (logged in start)
}

void PreviewEngine::start() {
    if (m_active) return;
    if (m_camera) stop();   // clear a stale failed-start camera before retrying
    if (!available()) {
        emit logLine("warn", unavailableReason());
        return;
    }

    const int ri = (m_resIndex >= 0 && m_resIndex < kPreviewResCount) ? m_resIndex : 0;

    m_camera = std::make_unique<QCamera>(m_device);
    // Honest failure path: device busy (another app holds the node), backend
    // errors, permission problems — stop and say why, never freeze a frame.
    // QUEUED: stop() destroys the QCamera, and destroying the sender from
    // inside its own (direct) signal emission is a use-after-free — defer the
    // teardown to the next event-loop pass. Because a queued delivery survives
    // both disconnect() and sender destruction, the lambda is tagged with the
    // camera GENERATION it came from and bails if the engine has since moved on
    // (otherwise a stale error from camera A would stop() a freshly restarted
    // camera B, e.g. right after a resolution change). A generation counter —
    // not a captured pointer — so an allocator reusing the old address can't
    // false-match.
    const quint64 origin = ++m_camGeneration;
    connect(m_camera.get(), &QCamera::errorOccurred, this,
            [this, origin](QCamera::Error err, const QString &msg) {
                if (err == QCamera::NoError || origin != m_camGeneration) return;
                emit logLine("err", QStringLiteral("preview: %1").arg(
                                        msg.isEmpty() ? QStringLiteral("capture error") : msg));
                stop();
            },
            Qt::QueuedConnection);
    // Mirror the backend's real active state (start() is asynchronous), and log
    // the NEGOTIATED format once streaming — the honest ground truth of what the
    // device is actually delivering (vs. what we requested).
    connect(m_camera.get(), &QCamera::activeChanged, this, [this](bool active) {
        if (active && m_camera) {
            const QCameraFormat f = m_camera->cameraFormat();
            if (!f.isNull())
                emit logLine("ok", QStringLiteral("preview: streaming %1x%2 @ %3–%4 fps (negotiated)")
                                       .arg(f.resolution().width()).arg(f.resolution().height())
                                       .arg(f.minFrameRate(), 0, 'f', 0).arg(f.maxFrameRate(), 0, 'f', 0));
        }
        if (m_active == active) return;
        m_active = active;
        emit activeChanged();
    });

    const QCameraFormat fmt = pickFormat();
    if (!fmt.isNull())
        m_camera->setCameraFormat(fmt);

    m_session.setVideoSink(m_sink);
    m_session.setCamera(m_camera.get());
    m_camera->start();
    emit logLine("cmd", QStringLiteral("preview: starting embedded stream at %1 (MJPG)")
                            .arg(QLatin1String(kPreviewRes[ri].label)));
}

void PreviewEngine::stop() {
    if (!m_camera) return;
    m_camera->disconnect(this);   // no error/active callbacks during teardown
    m_camera->stop();
    m_session.setCamera(nullptr);
    m_camera.reset();
    if (m_active) {
        m_active = false;
        emit activeChanged();
        emit logLine("sys", QStringLiteral("preview: stopped"));
    }
}

void PreviewEngine::toggle() {
    if (m_active || m_camera) stop();
    else start();
}

void PreviewEngine::setResIndex(int idx) {
    if (idx < 0 || idx >= kPreviewResCount || idx == m_resIndex) return;
    m_resIndex = idx;
    // A UVC mode change needs stream renegotiation — restart if running.
    if (m_camera) {
        stop();
        start();
    }
}
