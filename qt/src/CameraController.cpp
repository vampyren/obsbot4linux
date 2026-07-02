#include "CameraController.h"
#include "CameraWorker.h"
#include "PreviewFormats.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {
// Mirror of the SDK's AiWorkModeType values we use (see dev.hpp:554). Kept as
// local constants so this GUI-thread TU doesn't need the SDK headers.
constexpr int kAiNone = 0;    // AiWorkModeNone
constexpr int kAiHuman = 2;   // AiWorkModeHuman (single-person tracking)
constexpr int kAiSwitching = 6;

// NOTE: contains a UTF-8 degree sign — convert with fromUtf8, never fromLatin1
// (fromLatin1 renders it as the "78Â°" mojibake Rex saw in the log).
const char *kFovLabels[] = {"Wide 86°", "Medium 78°", "Narrow 65°"};
// (Preview resolutions live in PreviewFormats.h — shared with PreviewEngine.)

// Settle delay before the automatic "return to preset after AI off" move: the
// gimbal keeps disengaging from AI for ~1 s after the off command is accepted,
// and an immediate position move gets eaten or truncated.
constexpr int kAiReturnDelayMs = 1500;
// Window after a confirmed AI-Track ON in which a device-side drop back to None
// is reported as "couldn't lock on" (the Tiny 3 accepts the command but silently
// disengages when no person is in view).
constexpr qint64 kAiDisengageHintMs = 10000;
} // namespace

CameraController::CameraController(QObject *parent) : QObject(parent) {
    m_settings = Settings::load();
    m_aiModeName = QStringLiteral("Off");

    // ffplay fallback availability: ffplay present AND a /dev/video0 node exists.
    // (The embedded preview has its own by-name device detection in PreviewEngine.)
    m_previewAvailable = !QStandardPaths::findExecutable("ffplay").isEmpty()
                         && QFileInfo::exists("/dev/video0");

    m_pendingTimer = new QTimer(this);
    m_pendingTimer->setSingleShot(true);
    m_pendingTimer->setInterval(4000);   // safety: never leave AI 'pending' stuck
    connect(m_pendingTimer, &QTimer::timeout, this, [this]() {
        m_aiInFlight = 0;
        if (m_aiPending) { m_aiPending = false; emit aiChanged(); }
    });

    m_worker = new CameraWorker;   // no parent: it will live on m_thread
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &CameraWorker::init);

    m_gesture = m_settings.gesture;   // restore last gesture intent

    connect(m_worker, &CameraWorker::logLine, this, &CameraController::logLine);
    connect(m_worker, &CameraWorker::connectionResolved, this, &CameraController::onConnectionResolved);
    connect(m_worker, &CameraWorker::deviceLost, this, &CameraController::onDeviceLost);
    connect(m_worker, &CameraWorker::statusUpdate, this, &CameraController::onStatusUpdate);
    connect(m_worker, &CameraWorker::auxStatus, this, &CameraController::onAuxStatus);
    connect(m_worker, &CameraWorker::zoomUpdate, this, &CameraController::onZoomUpdate);
    connect(m_worker, &CameraWorker::imageParams, this, &CameraController::onImageParams);
    connect(m_worker, &CameraWorker::commandResult, this, &CameraController::onWorkerResult);
    connect(m_worker, &CameraWorker::presetCaptured, this, &CameraController::onPresetCaptured);

    m_thread.start();
}

CameraController::~CameraController() {
    stopPreview();   // kill the external ffplay so it doesn't linger after the app closes
    // Optional "sleep on exit": issue it BLOCKING on the worker (which is still
    // running its event loop here) so the command completes before we tear the
    // worker down. Guarded by connected() so we never touch a dead handle.
    if (m_worker && connected() && m_settings.sleepOnExit)
        QMetaObject::invokeMethod(m_worker, "cmdSleep", Qt::BlockingQueuedConnection);
    // Deterministic teardown (CODE_REVIEW #1/#2/#3/#9): disable the SDK callback
    // and release the device on the worker thread while its event loop is still
    // alive, THEN quit and join. No detached threads, no post-loop UAF.
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    delete m_worker;
    m_worker = nullptr;
}

void CameraController::start(int waitMs) {
    m_connState = Discovering;
    emit connStateChanged();
    QMetaObject::invokeMethod(m_worker, "startDiscovery", Qt::QueuedConnection, Q_ARG(int, waitMs));
}

// ---------------------------------------------------------------------------
// Derived getters
// ---------------------------------------------------------------------------
bool CameraController::aiTracking() const { return m_aiPending ? m_targetTracking : m_aiTracking; }
bool CameraController::faceFocus() const { return m_aiPending ? m_targetFace : m_faceFocus; }
bool CameraController::gesture() const { return m_gesture; }

QString CameraController::previewRes() const {
    const int i = m_settings.previewResIndex;
    return (i >= 0 && i < kPreviewResCount) ? QString::fromLatin1(kPreviewRes[i].label)
                                            : QStringLiteral("—");
}

QString CameraController::appVersion() const {
#ifdef APP_VERSION
    return QStringLiteral(APP_VERSION);
#else
    return QStringLiteral("0.0.0");
#endif
}

double CameraController::speedValue() const {
    // Reference gimbal speed (deg/s) for step moves (gimbalSetSpeedPositionR) and
    // the joystick velocity scale. Speed = HOW FAST the gimbal travels; the move
    // DISTANCE is set solely by the Step selection. Medium was 40 deg/s, which is
    // so fast for a small step (~0.12s) that the gimbal's accel/decel made the
    // move stutter and undershoot; 30 keeps it clearly faster than Slow but
    // smooth. (Tuning value — refine from hardware feel.)
    return m_settings.speedMode == 1 ? 30.0   // Medium
                                     : 20.0;  // Slow
}

QString CameraController::decodeAiMode(int raw) const {
    switch (raw) {
    case 0:  return QStringLiteral("Off");
    case 1:  return QStringLiteral("Group track");
    case 2:  return QStringLiteral("Human track");
    case 3:  return QStringLiteral("Hand track");
    case 4:  return QStringLiteral("Whiteboard");
    case 5:  return QStringLiteral("Desk");
    case 6:  return QStringLiteral("Switching…");
    case 7:  return QStringLiteral("Speech");
    case 14: return QStringLiteral("Portrait track");
    case 15: return QStringLiteral("Customize");
    default: return QStringLiteral("Mode %1").arg(raw);   // honest, never blank (#7)
    }
}

QVariantList CameraController::presets() const {
    QVariantList out;
    for (int i = 0; i < 3; ++i) {
        const PresetData &p = m_settings.presets[i];
        QVariantMap m;
        m["index"] = i;
        m["set"] = p.set;
        m["name"] = p.name.isEmpty() ? QStringLiteral("Preset %1").arg(i + 1) : p.name;
        m["pan"] = p.pan;
        m["tilt"] = p.tilt;
        m["zoom"] = p.zoom;
        m["fov"] = p.fov;
        m["summary"] = p.set ? QStringLiteral("pan %1° · tilt %2° · %3x")
                                   .arg(p.pan, 0, 'f', 0).arg(p.tilt, 0, 'f', 0).arg(p.zoom, 0, 'f', 2)
                             : QStringLiteral("Empty slot");
        out.append(m);
    }
    return out;
}

void CameraController::persist() {
    if (!Settings::save(m_settings))
        emit logLine("warn", QStringLiteral("settings: failed to write %1").arg(Settings::configPath()));
}

// ---------------------------------------------------------------------------
// Persisted setters
// ---------------------------------------------------------------------------
void CameraController::setMoveStepDeg(int deg) {
    if (deg == m_settings.moveStepDeg) return;
    m_settings.moveStepDeg = deg;
    persist();
    emit settingsChanged();
}
void CameraController::setSpeedMode(int mode) {
    if (mode == m_settings.speedMode) return;
    m_settings.speedMode = mode;
    persist();
    emit settingsChanged();
}
void CameraController::setStartupPreset(int p) {
    if (p < 0 || p > 3 || p == m_settings.startupPreset) return;
    m_settings.startupPreset = p;
    persist();
    emit settingsChanged();
}
void CameraController::resetImageDefaults() {
    setImageParam(QStringLiteral("brightness"), 50);
    setImageParam(QStringLiteral("contrast"), 50);
    setImageParam(QStringLiteral("saturation"), 50);
    setImageParam(QStringLiteral("sharpness"), 50);
    emit logLine("cmd", QStringLiteral("image: reset to defaults (50)"));
}
void CameraController::setFovIndex(int idx) {
    if (idx < 0 || idx > 2) return;
    if (idx != m_settings.fovIndex) {
        m_settings.fovIndex = idx;
        persist();
        emit settingsChanged();
    }
    if (connected())
        QMetaObject::invokeMethod(m_worker, "cmdSetFov", Qt::QueuedConnection,
                                  Q_ARG(int, idx), Q_ARG(QString, QString::fromUtf8(kFovLabels[idx])));
}
void CameraController::setAiReturnPreset(int p) {
    if (p < 0 || p > 3 || p == m_settings.aiReturnPreset) return;
    m_settings.aiReturnPreset = p;
    persist();
    emit settingsChanged();
}
void CameraController::setPreviewResIndex(int idx) {
    if (idx < 0 || idx >= kPreviewResCount || idx == m_settings.previewResIndex) return;
    m_settings.previewResIndex = idx;
    persist();
    // The EMBEDDED preview restarts at the new mode via main.cpp syncing
    // PreviewEngine off this signal.
    emit settingsChanged();
    // The ffplay FALLBACK (if open) is reloaded here — it can't switch mid-stream.
    if (m_previewProc)
        launchPreview();
}
void CameraController::setSleepOnExit(bool on) {
    if (on == m_settings.sleepOnExit) return;
    m_settings.sleepOnExit = on;
    persist();
    emit settingsChanged();
}

// ---------------------------------------------------------------------------
// User actions
// ---------------------------------------------------------------------------
void CameraController::wake() { QMetaObject::invokeMethod(m_worker, "cmdWake", Qt::QueuedConnection); }
void CameraController::sleep() { QMetaObject::invokeMethod(m_worker, "cmdSleep", Qt::QueuedConnection); }
void CameraController::center() { QMetaObject::invokeMethod(m_worker, "cmdCenter", Qt::QueuedConnection); }

void CameraController::nudge(int dir) {
    QMetaObject::invokeMethod(m_worker, "cmdNudge", Qt::QueuedConnection,
                              Q_ARG(int, dir),
                              Q_ARG(double, static_cast<double>(m_settings.moveStepDeg)),
                              Q_ARG(double, speedValue()));
}

void CameraController::zoomIn() {
    QMetaObject::invokeMethod(m_worker, "cmdZoom", Qt::QueuedConnection,
                              Q_ARG(double, 0.1), Q_ARG(bool, false), Q_ARG(double, 0.0),
                              Q_ARG(QString, QStringLiteral("zoom in")));
}
void CameraController::zoomOut() {
    QMetaObject::invokeMethod(m_worker, "cmdZoom", Qt::QueuedConnection,
                              Q_ARG(double, -0.1), Q_ARG(bool, false), Q_ARG(double, 0.0),
                              Q_ARG(QString, QStringLiteral("zoom out")));
}
void CameraController::zoomReset() {
    // Absolute reset — succeeds even if the getter momentarily fails (#10).
    QMetaObject::invokeMethod(m_worker, "cmdZoom", Qt::QueuedConnection,
                              Q_ARG(double, 0.0), Q_ARG(bool, true), Q_ARG(double, 1.0),
                              Q_ARG(QString, QStringLiteral("zoom 1x")));
}

void CameraController::setAiTracking(bool on) {
    if (on == aiTracking()) { emit aiChanged(); return; }
    m_targetTracking = on;
    m_targetFace = faceFocus();      // preserve the independent face-focus leg's display
    ++m_aiInFlight;
    m_aiPending = true;              // suppress status-push fighting until confirmed (#8)
    m_pendingTimer->start();
    QMetaObject::invokeMethod(m_worker, "cmdSetAi", Qt::QueuedConnection,
                              Q_ARG(int, on ? kAiHuman : kAiNone), Q_ARG(int, 0),
                              Q_ARG(QString, on ? QStringLiteral("ai track on")
                                                : QStringLiteral("ai track off")));
    emit aiChanged();
}

void CameraController::setFaceFocus(bool on) {
    if (on == m_faceFocus && !m_aiPending) { emit aiChanged(); return; }
    m_targetFace = on;
    m_targetTracking = aiTracking();     // preserve the AI-track leg's display
    ++m_aiInFlight;
    m_aiPending = true;
    m_pendingTimer->start();
    QMetaObject::invokeMethod(m_worker, "cmdSetFace", Qt::QueuedConnection, Q_ARG(bool, on));
    emit aiChanged();
}

void CameraController::gimbalVelocity(double pitchFrac, double yawFrac) {
    pitchFrac = pitchFrac < -1.0 ? -1.0 : (pitchFrac > 1.0 ? 1.0 : pitchFrac);
    yawFrac = yawFrac < -1.0 ? -1.0 : (yawFrac > 1.0 ? 1.0 : yawFrac);
    const double maxSpeed = speedValue();   // reuse the Slow/Medium reference speed, deg/s
    QMetaObject::invokeMethod(m_worker, "cmdGimbalVelocity", Qt::QueuedConnection,
                              Q_ARG(double, pitchFrac * maxSpeed), Q_ARG(double, yawFrac * maxSpeed));
}

void CameraController::gimbalStop() {
    QMetaObject::invokeMethod(m_worker, "cmdGimbalStop", Qt::QueuedConnection);
}

void CameraController::setGesture(bool on) {
    m_targetGesture = on;
    m_gesture = on;   // optimistic; reverted in onWorkerResult on failure
    m_settings.gesture = on;
    persist();
    QMetaObject::invokeMethod(m_worker, "cmdSetGesture", Qt::QueuedConnection, Q_ARG(bool, on));
    emit aiChanged();
}

void CameraController::setHdr(bool on) {
    if (!m_hdrSupport) {
        emit logLine("warn", QStringLiteral("hdr: device reports HDR not supported in this mode"));
        return;
    }
    m_targetHdr = on;
    m_hdrOn = on;        // optimistic; onWorkerResult confirms/reverts
    m_hdrPending = true; // suppress the status push clobbering the optimistic value (#8)
    QMetaObject::invokeMethod(m_worker, "cmdSetHdr", Qt::QueuedConnection, Q_ARG(bool, on));
    emit imageChanged();
}

void CameraController::setImageParam(const QString &param, int value) {
    // Update the local mirror immediately for responsive UI; the device call
    // follows. (These params are not in the status push, so the mirror is the
    // source of truth; a getter re-read on connect keeps it honest.)
    if (param == QLatin1String("brightness")) m_brightness = value;
    else if (param == QLatin1String("contrast")) m_contrast = value;
    else if (param == QLatin1String("saturation")) m_saturation = value;
    else if (param == QLatin1String("sharpness")) m_sharpness = value;
    emit imageChanged();
    QMetaObject::invokeMethod(m_worker, "cmdSetImage", Qt::QueuedConnection,
                              Q_ARG(QString, param), Q_ARG(int, value));
}

void CameraController::rescan() {
    m_connState = Discovering;
    emit connStateChanged();
    QMetaObject::invokeMethod(m_worker, "rescan", Qt::QueuedConnection, Q_ARG(int, 6000));
}

void CameraController::launchPreview() {
    // FALLBACK path: real frames in a SEPARATE ffplay window. Kept alongside the
    // embedded preview (PreviewEngine) for boxes where QtMultimedia misbehaves.
    // Occupies the UVC node — conflicts with the embedded preview and with
    // browser/Meet/OBS camera use, exactly like any other capture client.
    if (!m_previewAvailable) {
        emit logLine("warn", QStringLiteral("preview: ffplay or /dev/video0 not available"));
        return;
    }
    stopPreview();   // kill any existing preview first (also used to reload on res change)

    const int ri = (m_settings.previewResIndex >= 0 && m_settings.previewResIndex < kPreviewResCount)
                       ? m_settings.previewResIndex : 0;
    const PreviewRes &pr = kPreviewRes[ri];

    // Run ffplay directly (not via a shell, not detached) so the process is a
    // managed child we can terminate cleanly. -loglevel error silences the
    // harmless per-frame MJPEG/swscaler warnings ("EOI missing", "deprecated
    // pixel format") the camera's MJPEG stream produces.
    m_previewProc = new QProcess(this);
    connect(m_previewProc, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (m_previewProc) { m_previewProc->deleteLater(); m_previewProc = nullptr; }
        emit logLine("sys", QStringLiteral("preview: ffplay closed"));
    });
    const QStringList args = {
        "-hide_banner", "-loglevel", "error", "-fflags", "nobuffer",
        "-f", "v4l2", "-input_format", "mjpeg",
        "-video_size", QStringLiteral("%1x%2").arg(pr.w).arg(pr.h),
        "-framerate", QString::number(pr.fps),
        "-window_title", QStringLiteral("OBSBOT preview (%1)").arg(QString::fromLatin1(pr.label)),
        "/dev/video0"};
    m_previewProc->start(QStringLiteral("ffplay"), args);
    emit logLine("cmd", QStringLiteral("preview: launched ffplay at %1 (external window, fallback)")
                            .arg(QString::fromLatin1(pr.label)));
}

void CameraController::copyToClipboard(const QString &text) {
    if (auto *cb = QGuiApplication::clipboard()) {
        cb->setText(text);
        emit logLine("sys", QStringLiteral("log copied to clipboard (%1 chars)").arg(text.size()));
    }
}

void CameraController::stopPreview() {
    if (!m_previewProc) return;
    QProcess *p = m_previewProc;
    m_previewProc = nullptr;
    disconnect(p, nullptr, this, nullptr);   // don't let the finished-lambda re-fire during teardown
    p->terminate();
    if (!p->waitForFinished(800)) {
        p->kill();
        // Wait for the kill to land too — callers may start the EMBEDDED
        // preview immediately after this returns, and a still-dying ffplay
        // would hold the UVC node and bounce it with "device busy".
        p->waitForFinished(500);
    }
    p->deleteLater();
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
void CameraController::savePreset(int idx) {
    if (idx < 0 || idx > 2) return;
    QMetaObject::invokeMethod(m_worker, "cmdPresetCapture", Qt::QueuedConnection, Q_ARG(int, idx));
}
void CameraController::goPreset(int idx) {
    if (idx < 0 || idx > 2) return;
    const PresetData &p = m_settings.presets[idx];
    if (!p.set) {
        emit logLine("warn", QStringLiteral("preset %1: empty — Save first").arg(idx + 1));
        return;
    }
    QMetaObject::invokeMethod(m_worker, "cmdPresetGo", Qt::QueuedConnection,
                              Q_ARG(int, idx), Q_ARG(double, p.tilt), Q_ARG(double, p.pan),
                              Q_ARG(double, p.zoom), Q_ARG(int, p.fov), Q_ARG(double, speedValue()));
}
void CameraController::clearPreset(int idx) {
    if (idx < 0 || idx > 2) return;
    m_settings.presets[idx] = PresetData{};
    persist();
    emit presetsChanged();
    emit logLine("cmd", QStringLiteral("preset %1 cleared").arg(idx + 1));
}
void CameraController::renamePreset(int idx, const QString &name) {
    if (idx < 0 || idx > 2) return;
    m_settings.presets[idx].name = name;
    persist();
    emit presetsChanged();
}
void CameraController::saveCurrentToNextEmpty() {
    for (int i = 0; i < 3; ++i) {
        if (!m_settings.presets[i].set) { savePreset(i); return; }
    }
    emit logLine("warn", QStringLiteral("presets: all slots full — overwrite one instead"));
}

// ---------------------------------------------------------------------------
// Worker signal handlers (GUI thread)
// ---------------------------------------------------------------------------
void CameraController::onConnectionResolved(bool found, const QString &product, const QString &sn,
                                            const QString &fw, const QString &mode, int enumId) {
    if (found) {
        m_connState = Connected;
        m_product = product; m_sn = sn; m_firmware = fw; m_mode = mode; m_enumId = enumId;
        emit identityChanged();
        // Align the device's FOV with the restored UI setting (benign, no motion).
        QMetaObject::invokeMethod(m_worker, "cmdSetFov", Qt::QueuedConnection,
                                  Q_ARG(int, m_settings.fovIndex),
                                  Q_ARG(QString, QString::fromUtf8(kFovLabels[m_settings.fovIndex])));
        // Re-apply the persisted gesture-control choice to the DEVICE. Restoring
        // it only into the UI (constructor) left the chip showing ON while the
        // camera actually had gesture off — gestures "didn't detect" all session
        // unless the user re-toggled (Rex's hardware finding). rc is logged.
        // m_targetGesture must match, or the failure leg in onWorkerResult would
        // "revert" to !false and confirm the chip ON — the very bug being fixed.
        // Delayed like the startup preset: right at connect the device still
        // eats commands (rc=0 but no effect) — the same too-early window that
        // forces kAiReturnDelayMs and the 1600 ms preset settle.
        if (m_gesture) {
            QTimer::singleShot(kAiReturnDelayMs, this, [this]() {
                if (!connected() || !m_gesture) return;
                m_targetGesture = true;
                QMetaObject::invokeMethod(m_worker, "cmdSetGesture", Qt::QueuedConnection,
                                          Q_ARG(bool, true));
            });
        }
        // Startup preset: move to the chosen preset on a GENUINE connect only —
        // after a delay so the gimbal's power-on centering finishes first (see
        // scheduleStartupPreset). A Rescan while already connected re-binds the
        // same device and re-emits connectionResolved(true); m_hadDevice guards
        // against re-firing the preset (and moving the gimbal) in that case.
        if (!m_hadDevice)
            scheduleStartupPreset(QStringLiteral("startup"));
        m_hadDevice = true;
    } else {
        m_connState = Disconnected;
    }
    emit connStateChanged();
    emit statusChanged();
    emit discoveryFinished(found);
}

void CameraController::onDeviceLost(const QString &reason) {
    m_connState = Disconnected;
    m_runState = RunUnknown;
    m_aiTracking = false;
    m_faceFocus = false;
    m_hdrOn = false;
    m_hdrSupport = false;
    m_hdrPending = false;
    m_aiPending = false;
    m_aiInFlight = 0;
    m_aiEngageTime.invalidate();
    m_hadDevice = false;   // a real loss: the next connect is genuine → preset re-fires
    m_zoomValid = false;
    emit connStateChanged();
    emit statusChanged();
    emit aiChanged();
    emit imageChanged();
    emit zoomChanged();
    emit logLine("warn", QStringLiteral("device lost: %1 — controls disabled").arg(reason));
}

void CameraController::onStatusUpdate(int runState, int aiModeRaw, double zoom, bool zoomValid) {
    const int prevRun = m_runState;
    if (runState != m_runState) { m_runState = runState; emit statusChanged(); }
    // Wake edge (Asleep → Awake): re-apply the startup preset. The camera
    // re-centers its gimbal on wake, so this restores the user's chosen position
    // after they wake it — mirroring the on-connect behavior.
    if (prevRun == Asleep && runState == Awake)
        scheduleStartupPreset(QStringLiteral("wake"));

    m_zoom = zoom; m_zoomValid = zoomValid; emit zoomChanged();

    m_aiModeRaw = aiModeRaw;
    m_aiModeName = decodeAiMode(aiModeRaw);
    // Resync confirmed AI Track from the device UNLESS a toggle is in flight (#8)
    // or the device is mid-switch (#5). Any settled ai_mode > None means tracking
    // is engaged (AI Track owns the gimbal).
    if (!m_aiPending && aiModeRaw != kAiSwitching) {
        const bool was = m_aiTracking;
        m_aiTracking = (aiModeRaw > kAiNone);
        // Device-side disengage right after we enabled tracking: the Tiny 3
        // accepts AI-Track ON (rc=0) but silently drops back to None when it
        // can't lock onto a person. Without this hint the chip just flips off
        // and the user is left guessing — say what happened instead.
        if (was && !m_aiTracking
            && m_aiEngageTime.isValid() && m_aiEngageTime.elapsed() < kAiDisengageHintMs) {
            emit logLine("warn", QStringLiteral(
                "ai track: device disengaged itself right after enabling — it needs a "
                "person in view to lock on. Aim the camera at yourself and try again."));
        }
        if (!m_aiTracking) m_aiEngageTime.invalidate();
    }
    emit aiChanged();
}

void CameraController::onAuxStatus(bool faceFocus, bool hdrOn, bool hdrSupport, int fps) {
    // Face autofocus + HDR are reported by the device; sync them honestly (unless
    // an AI toggle is mid-flight, in which case the optimistic target stands).
    if (!m_aiPending) m_faceFocus = faceFocus;
    if (!m_hdrPending) m_hdrOn = hdrOn;   // don't clobber an in-flight HDR toggle (#8)
    m_hdrSupport = hdrSupport;
    if (fps != m_fps) { m_fps = fps; emit statusChanged(); }
    emit aiChanged();
    emit imageChanged();
}

void CameraController::onImageParams(int brightness, int contrast, int saturation, int sharpness) {
    m_brightness = brightness;
    m_contrast = contrast;
    m_saturation = saturation;
    m_sharpness = sharpness;
    emit imageChanged();
}

void CameraController::onZoomUpdate(double zoom, bool valid) {
    m_zoom = zoom; m_zoomValid = valid;
    emit zoomChanged();
}

void CameraController::onWorkerResult(const QString &action, bool ok, int /*rc*/, const QString &message) {
    // AI Track leg confirmation: apply target on success, discard on failure (#5/#7).
    if (action.startsWith("ai track")) {
        if (ok) m_aiTracking = m_targetTracking;
        // Arm the device-disengage detector on a confirmed ON (see onStatusUpdate);
        // a deliberate OFF disarms it so it can't fire a bogus hint.
        if (ok && action.endsWith("on") && m_targetTracking) m_aiEngageTime.start();
        else if (action.endsWith("off"))                     m_aiEngageTime.invalidate();
        if (--m_aiInFlight <= 0) { m_aiInFlight = 0; m_aiPending = false; m_pendingTimer->stop(); }
        emit aiChanged();
        // "After AI off, go to preset": when turning AI Track OFF is confirmed and
        // the user chose a return preset, recall it — after a settle delay. The
        // gimbal is still physically disengaging from AI for ~1 s after the off
        // command is accepted; an immediate move gets eaten or truncated (Rex's
        // hardware finding: "moves a tiny bit" / doesn't reach the preset). Same
        // fix pattern as the startup-preset delay. Re-checked at fire time in
        // case AI was switched back on during the wait.
        const int rp = m_settings.aiReturnPreset;
        if (ok && action.endsWith("off") && !m_targetTracking
            && rp >= 1 && rp <= 3 && m_settings.presets[rp - 1].set) {
            emit logLine("cmd", QStringLiteral("ai off: returning to preset %1 in %2 ms (gimbal settle)")
                                    .arg(rp).arg(kAiReturnDelayMs));
            QTimer::singleShot(kAiReturnDelayMs, this, [this, rp]() {
                if (!connected()) return;
                if (asleep()) {
                    emit logLine("warn", QStringLiteral("ai off: return to preset %1 skipped — camera asleep").arg(rp));
                    return;
                }
                if (aiTracking()) {
                    emit logLine("warn", QStringLiteral("ai off: return to preset %1 skipped — AI is on again").arg(rp));
                    return;
                }
                goPreset(rp - 1);
            });
        }
    } else if (action == QLatin1String("face focus")) {
        if (ok) m_faceFocus = m_targetFace;
        if (--m_aiInFlight <= 0) { m_aiInFlight = 0; m_aiPending = false; m_pendingTimer->stop(); }
        emit aiChanged();
    } else if (action == QLatin1String("gesture")) {
        if (!ok) { m_gesture = !m_targetGesture; m_settings.gesture = m_gesture; persist(); }  // revert
        emit aiChanged();
    } else if (action == QLatin1String("hdr")) {
        if (ok) m_hdrOn = m_targetHdr;
        else    m_hdrOn = !m_targetHdr;   // revert
        m_hdrPending = false;             // toggle settled: allow status resync again
        emit imageChanged();
    }
    emit commandResult(action, ok, message);
}

void CameraController::onPresetCaptured(int idx, double pitch, double yaw, double zoom, int fov) {
    if (idx < 0 || idx > 2) return;
    PresetData &p = m_settings.presets[idx];
    const QString name = p.name.isEmpty() ? QStringLiteral("Preset %1").arg(idx + 1) : p.name;
    p.set = true;
    p.name = name;
    p.pan = yaw;      // pan  = yaw
    p.tilt = pitch;   // tilt = pitch
    p.zoom = zoom;
    p.fov = (fov < 0) ? m_settings.fovIndex : fov;
    persist();
    emit presetsChanged();
}

void CameraController::scheduleStartupPreset(const QString &why) {
    const int sp = m_settings.startupPreset;
    if (sp < 1 || sp > 3 || !m_settings.presets[sp - 1].set)
        return;
    emit logLine("cmd", QStringLiteral("%1: going to preset %2 (after gimbal settles)").arg(why).arg(sp));
    // Delay so the camera's power-on / wake self-centering completes first —
    // otherwise it overrides the preset move and the gimbal ends up centered
    // instead of at the preset. Context object is `this`, so a destroyed
    // controller cancels the pending fire.
    QTimer::singleShot(1600, this, [this, sp]() {
        if (connected() && !m_aiTracking)
            goPreset(sp - 1);
    });
}
