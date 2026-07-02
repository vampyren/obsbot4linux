#include "CameraWorker.h"

#include <QMetaObject>
#include <QTimer>

#include <cmath>
#include <string>

#include <dev/devs.hpp>

namespace {

// Gimbal safety clamps (degrees) — identical bounds to the validated GTK PoC.
constexpr float kPitchMin = -90.0f, kPitchMax = 90.0f;
constexpr float kYawMin = -120.0f, kYawMax = 120.0f;
// Stale-AI-status grace after a confirmed AI-off (device pushes lag 2–3 s).
constexpr qint64 kAiOffGraceMs = 4000;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// RunState enum mirrored by CameraController: 0=Unknown, 1=Awake, 2=Asleep.
int runStateFromDev(int devStatus) {
    switch (devStatus) {
    case Device::DevStatusRun:   return 1;   // Awake
    case Device::DevStatusSleep: return 2;   // Asleep
    default:                     return 0;   // Unknown / Privacy
    }
}

const char *productName(ObsbotProductType t) {
    switch (t) {
    case ObsbotProdTiny:      return "Tiny";
    case ObsbotProdTiny4k:    return "Tiny4K";
    case ObsbotProdTiny2:     return "Tiny2";
    case ObsbotProdTiny2Lite: return "Tiny2Lite";
    case ObsbotProdTinySE:    return "TinySE";
    case ObsbotProdTiny3:     return "Tiny3";
    case ObsbotProdTiny3Lite: return "Tiny3Lite";
    case ObsbotProdMeet:      return "Meet";
    case ObsbotProdMeet4k:    return "Meet4K";
    case ObsbotProdTailAir:   return "TailAir";
    default:                  return "Unknown";
    }
}

const char *devModeName(Device::DevMode m) {
    switch (m) {
    case Device::DevModeUvc: return "UVC";
    case Device::DevModeNet: return "Net";
    case Device::DevModeMtp: return "MTP";
    case Device::DevModeBle: return "BLE";
    default:                 return "Unknown";
    }
}

} // namespace

CameraWorker::CameraWorker(QObject *parent) : QObject(parent) {}

CameraWorker::~CameraWorker() = default;

void CameraWorker::init() {
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(200);
    connect(m_pollTimer, &QTimer::timeout, this, &CameraWorker::pollTick);

    // Deadman stop: if no fresh velocity command arrives within this window
    // (e.g. the UI thread stalls, or the drag handler fails to deliver a stop),
    // halt the gimbal automatically. Restarted on every cmdGimbalVelocity call.
    m_velocityWatchdog = new QTimer(this);
    m_velocityWatchdog->setSingleShot(true);
    m_velocityWatchdog->setInterval(400);
    connect(m_velocityWatchdog, &QTimer::timeout, this, [this]() {
        if (m_velocityActive) {
            emit logLine("warn", QStringLiteral("ptz velocity: deadman timeout — auto-stopped"));
            cmdGimbalStop();
        }
    });
}

// ---------------------------------------------------------------------------
// Discovery (non-blocking poll on the worker's own event loop)
// ---------------------------------------------------------------------------
void CameraWorker::startDiscovery(int waitMs) {
    if (m_shuttingDown) return;
    m_pollTimeoutMs = waitMs > 0 ? waitMs : 6000;
    m_pollElapsedMs = 0;

    Devices &devs = Devices::get();
    devs.setEnableMdnsScan(false);   // USB only for this app

    // One-time device plug/unplug callback (hot-unplug handling, CODE_REVIEW #6).
    if (!m_devChangedRegistered) {
        devs.setDevChangedCallback(
            [this](std::string sn, bool plugged, void *) {
                const QString qsn = QString::fromStdString(sn);
                QMetaObject::invokeMethod(
                    this, [this, qsn, plugged]() { onDevChanged(qsn, plugged); },
                    Qt::QueuedConnection);
            },
            this);
        m_devChangedRegistered = true;
    }

    emit logLine("net", QStringLiteral("discovery: started (USB, timeout %1 ms)").arg(m_pollTimeoutMs));
    m_pollTimer->start();
}

void CameraWorker::rescan(int waitMs) {
    if (m_shuttingDown) return;
    emit logLine("net", QStringLiteral("rescan requested"));
    startDiscovery(waitMs);
}

void CameraWorker::pollTick() {
    if (m_shuttingDown) { m_pollTimer->stop(); return; }

    std::shared_ptr<Device> found;
    for (const auto &d : Devices::get().getDevList()) {
        if (d && !d->devSn().empty()) { found = d; break; }
    }
    if (found) {
        m_pollTimer->stop();
        bindDevice(found);
        return;
    }

    m_pollElapsedMs += m_pollTimer->interval();
    if (m_pollElapsedMs % 1000 == 0) {
        emit logLine("net", QStringLiteral("discovery: polling for device … (%1/%2 ms)")
                                .arg(m_pollElapsedMs).arg(m_pollTimeoutMs));
    }
    if (m_pollElapsedMs >= m_pollTimeoutMs) {
        m_pollTimer->stop();
        emit logLine("warn", QStringLiteral("discovery: timeout after %1 ms — no device").arg(m_pollTimeoutMs));
        emit connectionResolved(false, {}, {}, {}, {}, -1);
    }
}

void CameraWorker::bindDevice(const std::shared_ptr<Device> &d) {
    m_dev = d;
    m_sn = QString::fromStdString(d->devSn());

    const QString product = productName(d->productType());
    const QString fw = QString::fromStdString(d->devVersion());
    const QString mode = devModeName(d->devMode());
    const int enumId = static_cast<int>(d->productType());

    // Subscribe to the periodic status push (~2–3 s) for run/AI/zoom state.
    d->setDevStatusCallbackFunc(&CameraWorker::sdkStatusTrampoline, this);
    d->enableDevStatusCallback(true);

    emit logLine("ok", QStringLiteral("connected: %1  (SN %2, fw %3, %4)")
                           .arg(product, m_sn, fw, mode));
    emit connectionResolved(true, product, m_sn, fw, mode, enumId);

    // Read real zoom + current image params once now (blocking getters, safe here).
    refreshZoom();
    cmdReadImageParams();
}

// ---------------------------------------------------------------------------
// Status push: SDK thread -> hop to worker thread -> emit to GUI thread
// ---------------------------------------------------------------------------
void CameraWorker::sdkStatusTrampoline(void *param, const void *data) {
    if (!param || !data) return;
    auto *self = static_cast<CameraWorker *>(param);
    const auto *st = static_cast<const Device::CameraStatus *>(data);
    const int run = st->tiny.dev_status;
    const int ai = st->tiny.ai_mode;
    const int face = st->tiny.face_auto_focus;   // face autofocus 0/1
    const int hdr = st->tiny.hdr;                // hdr 0/1
    const int hdrSup = st->tiny.hdr_support;     // hdr supported in current mode 0/1
    const int fps = st->tiny.fps;                // current video stream fps
    // Do NOT touch SDK/Qt state here — just marshal onto the worker thread.
    QMetaObject::invokeMethod(
        self, [self, run, ai, face, hdr, hdrSup, fps]() { self->onSdkStatus(run, ai, face, hdr, hdrSup, fps); },
        Qt::QueuedConnection);
}

void CameraWorker::onSdkStatus(int runStatus, int aiMode, int faceFocus, int hdr, int hdrSupport, int fps) {
    if (m_shuttingDown || !m_dev) return;
    // aiMode>0 means some AI mode is engaged (includes AiWorkModeSwitching=6).
    // Treating "switching" as tracking is the safe choice for the gimbal guard.
    //
    // Stale-push suppression: right after a confirmed "AI off" the device keeps
    // reporting the old AI mode for up to a push cycle (2–3 s). Taking that at
    // face value re-arms the AI-owns-gimbal guard and blocks the moves users
    // expect immediately after turning AI off (incl. the automatic
    // return-to-preset). Inside the grace window, an AI-on push is treated as
    // stale: the guard stays released and the CORRECTED mode is forwarded so
    // the UI doesn't flicker back to "on" either.
    if (aiMode > Device::AiWorkModeNone
        && m_aiOffGrace.isValid() && m_aiOffGrace.elapsed() < kAiOffGraceMs) {
        aiMode = Device::AiWorkModeNone;   // stale — we KNOW the off command landed
    } else if (aiMode <= Device::AiWorkModeNone) {
        m_aiOffGrace.invalidate();         // device caught up; grace no longer needed
    }
    m_aiTracking = (aiMode > Device::AiWorkModeNone);

    // Refresh real zoom from the getter (1.0–2.0). Runs on the worker thread,
    // serialized with commands — never on the SDK callback thread.
    float z = 0.0f;
    const bool zok = (m_dev->cameraGetZoomAbsoluteR(z) == RM_RET_OK);
    emit statusUpdate(runStateFromDev(runStatus), aiMode, z, zok);
    emit auxStatus(faceFocus != 0, hdr != 0, hdrSupport != 0, fps);
}

void CameraWorker::onDevChanged(const QString &sn, bool plugged) {
    if (m_shuttingDown) return;
    if (!plugged) {
        // Unplug. If it's our device (or we can no longer see any), drop it.
        if (m_dev && (sn == m_sn || sn.isEmpty())) {
            emit logLine("warn", QStringLiteral("device unplugged (SN %1)").arg(m_sn));
            if (m_dev) m_dev->enableDevStatusCallback(false);
            m_dev.reset();
            m_aiTracking = false;
            emit deviceLost(QStringLiteral("USB unplug"));
        }
    } else if (!m_dev) {
        emit logLine("net", QStringLiteral("device plugged in (SN %1) — reconnecting").arg(sn));
        startDiscovery(m_pollTimeoutMs);
    }
}

// ---------------------------------------------------------------------------
// Command helpers
// ---------------------------------------------------------------------------
bool CameraWorker::requireDevice(const QString &action) {
    if (m_dev) return true;
    emit commandResult(action, false, -1, QStringLiteral("no device connected"));
    emit logLine("warn", action + QStringLiteral(": no device connected"));
    return false;
}

bool CameraWorker::aiOwnsGimbal(const QString &action) {
    if (!m_aiTracking) return false;
    // Honest: the device keeps the gimbal under AI control, so a manual move
    // would be ignored. Block it and say so — never log a fake success.
    const QString msg = QStringLiteral("blocked: AI tracking owns the gimbal — turn AI off first");
    emit commandResult(action, false, -1, msg);
    emit logLine("warn", action + QStringLiteral(": ") + msg);
    return true;
}

void CameraWorker::refreshZoom() {
    if (!m_dev) return;
    float z = 0.0f;
    const bool ok = (m_dev->cameraGetZoomAbsoluteR(z) == RM_RET_OK);
    emit zoomUpdate(z, ok);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
void CameraWorker::cmdWake() {
    const QString a = QStringLiteral("wake");
    if (!requireDevice(a)) return;
    emit logLine("cmd", QStringLiteral("→ wake"));
    const int rc = m_dev->cameraSetDevRunStatusR(Device::DevStatusRun);
    const bool ok = (rc == RM_RET_OK);
    emit commandResult(a, ok, rc, ok ? QStringLiteral("awake") : QStringLiteral("failed"));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("wake  rc=%1").arg(rc));
}

void CameraWorker::cmdSleep() {
    const QString a = QStringLiteral("sleep");
    if (!requireDevice(a)) return;
    emit logLine("cmd", QStringLiteral("→ sleep"));
    const int rc = m_dev->cameraSetDevRunStatusR(Device::DevStatusSleep);
    const bool ok = (rc == RM_RET_OK);
    emit commandResult(a, ok, rc, ok ? QStringLiteral("asleep") : QStringLiteral("failed"));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("sleep  rc=%1").arg(rc));
}

void CameraWorker::cmdCenter() {
    const QString a = QStringLiteral("center");
    if (!requireDevice(a)) return;
    if (aiOwnsGimbal(a)) return;                 // CODE_REVIEW #4
    emit logLine("cmd", QStringLiteral("→ center"));
    const int rc = m_dev->gimbalRstPosR();       // bounded reset to home
    const bool ok = (rc == RM_RET_OK);
    emit commandResult(a, ok, rc, ok ? QStringLiteral("centered") : QStringLiteral("failed"));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("center  rc=%1").arg(rc));
}

void CameraWorker::cmdNudge(int dir, double stepDeg, double speed) {
    const char *names[] = {"ptz up", "ptz down", "ptz left", "ptz right"};
    const QString a = QString::fromLatin1(dir >= 0 && dir < 4 ? names[dir] : "ptz");
    if (!requireDevice(a)) return;
    if (aiOwnsGimbal(a)) return;                 // CODE_REVIEW #4

    // Direction → axis sign. INVERTED vs the GTK PoC per Rex's hardware test
    // (Rex finding #1): up = -pitch, down = +pitch, left = -yaw, right = +yaw.
    float pitchSign = 0.0f, yawSign = 0.0f;
    switch (dir) {
    case 0: pitchSign = -1.0f; break;  // up
    case 1: pitchSign = +1.0f; break;  // down
    case 2: yawSign = -1.0f; break;    // left
    case 3: yawSign = +1.0f; break;    // right
    }

    const float step = static_cast<float>(stepDeg);
    const float spd = static_cast<float>(speed);
    emit logLine("cmd", QStringLiteral("→ %1 (%2°, spd %3)").arg(a).arg(step, 0, 'f', 0).arg(spd, 0, 'f', 0));

    float xyz[3] = {0, 0, 0};   // roll, pitch, pan(yaw)
    const int rc = m_dev->gimbalGetAttitudeInfoR(xyz);
    if (rc != RM_RET_OK) {
        emit commandResult(a, false, rc, QStringLiteral("read attitude failed"));
        emit logLine("warn", a + QStringLiteral(": read attitude failed rc=%1").arg(rc));
        return;
    }
    const float pitch = xyz[1], yaw = xyz[2];
    const float np = clampf(pitch + pitchSign * step, kPitchMin, kPitchMax);
    const float ny = clampf(yaw + yawSign * step, kYawMin, kYawMax);
    const int rc2 = m_dev->gimbalSetSpeedPositionR(0.0f, np, ny, 0.0f, spd, spd);
    const bool ok = (rc2 == RM_RET_OK);
    emit commandResult(a, ok, rc2,
                       QStringLiteral("pitch %1→%2, yaw %3→%4")
                           .arg(pitch, 0, 'f', 1).arg(np, 0, 'f', 1)
                           .arg(yaw, 0, 'f', 1).arg(ny, 0, 'f', 1));
    emit logLine(ok ? "ok" : "warn",
                 QStringLiteral("%1: pitch %2→%3, yaw %4→%5  rc=%6")
                     .arg(a).arg(pitch, 0, 'f', 1).arg(np, 0, 'f', 1)
                     .arg(yaw, 0, 'f', 1).arg(ny, 0, 'f', 1).arg(rc2));
}

void CameraWorker::cmdZoom(double delta, bool absolute, double absVal, const QString &action) {
    if (!requireDevice(action)) return;
    emit logLine("cmd", QStringLiteral("→ %1").arg(action));

    float cur = 1.0f;
    const bool haveCur = (m_dev->cameraGetZoomAbsoluteR(cur) == RM_RET_OK);

    float target;
    if (absolute) {
        // CODE_REVIEW #10: the 1x reset must set 1.0 even if the getter fails —
        // the target is a constant and does not depend on the current reading.
        target = static_cast<float>(absVal);
    } else {
        if (!haveCur) {
            emit commandResult(action, false, -1, QStringLiteral("read zoom failed"));
            emit logLine("warn", action + QStringLiteral(": read zoom failed"));
            return;
        }
        target = cur + static_cast<float>(delta);
    }
    target = clampf(target, 1.0f, 2.0f);

    const int rc = m_dev->cameraSetZoomAbsoluteR(target);
    const bool ok = (rc == RM_RET_OK);

    // Re-read the ACTUAL zoom after the command so the UI never sticks (Rex #6).
    refreshZoom();

    emit commandResult(action, ok, rc,
                       QStringLiteral("%1x → %2x")
                           .arg(haveCur ? cur : target, 0, 'f', 2).arg(target, 0, 'f', 2));
    emit logLine(ok ? "ok" : "warn",
                 QStringLiteral("%1: → %2x  rc=%3").arg(action).arg(target, 0, 'f', 2).arg(rc));
}

void CameraWorker::cmdSetFov(int fovType, const QString &label) {
    const QString a = QStringLiteral("fov");
    if (!requireDevice(a)) return;
    emit logLine("cmd", QStringLiteral("→ fov %1").arg(label));
    const int rc = m_dev->cameraSetFovU(static_cast<Device::FovType>(fovType));
    const bool ok = (rc == RM_RET_OK);
    emit commandResult(a, ok, rc, label);
    emit logLine(ok ? "ok" : "warn", QStringLiteral("fov %1  rc=%2").arg(label).arg(rc));
}

void CameraWorker::cmdSetAi(int mode, int subMode, const QString &action) {
    if (!requireDevice(action)) return;
    emit logLine("cmd", QStringLiteral("→ %1").arg(action));
    // For Human tracking, subMode is the AiSubModeType framing (Normal/Upper/Close-up).
    const int rc = m_dev->cameraSetAiModeU(static_cast<Device::AiWorkModeType>(mode), subMode);
    const bool ok = (rc == RM_RET_OK);
    if (ok) {
        m_aiTracking = (mode != Device::AiWorkModeNone);   // optimistic; status push confirms
        // Open/close the stale-push grace window (see onSdkStatus).
        if (mode == Device::AiWorkModeNone) m_aiOffGrace.start();
        else                                m_aiOffGrace.invalidate();
    }
    emit commandResult(action, ok, rc, ok ? QStringLiteral("applied") : QStringLiteral("rejected"));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("%1  rc=%2").arg(action).arg(rc));
}

void CameraWorker::cmdSetFace(bool on) {
    const QString a = QStringLiteral("face focus");
    if (!requireDevice(a)) return;
    emit logLine("cmd", QStringLiteral("→ face focus %1").arg(on ? "on" : "off"));
    const int rc = m_dev->cameraSetFaceFocusR(on);
    const bool ok = (rc == RM_RET_OK);
    emit commandResult(a, ok, rc, on ? QStringLiteral("on") : QStringLiteral("off"));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("face focus %1  rc=%2").arg(on ? "on" : "off").arg(rc));
}

void CameraWorker::cmdSetGesture(bool on) {
    const QString a = QStringLiteral("gesture");
    if (!requireDevice(a)) return;
    emit logLine("cmd", QStringLiteral("→ gesture %1").arg(on ? "on" : "off"));
    // The Tiny 3 is a "tail2 and later" product in SDK terms: the legacy
    // aiSetGestureCtrlIndividualR (category "tiny, tiny4k, tiny2 series, tail
    // air") is ACKed with rc=0 but has NO effect on it — hardware-verified
    // (gestures never detected all session despite rc=0 on every toggle).
    // Use the DevGestureParaType API instead: the MASTER gesture switch plus
    // the target-select gesture. Legacy call kept as a fallback for older
    // tinys that don't speak the para API.
    int rc = m_dev->aiSetGestureParaR(Device::DevGestureParaTypeGesture, on);
    const int rcSel = m_dev->aiSetGestureParaR(Device::DevGestureParaTypeTargetSelection, on);
    if (rc != RM_RET_OK && rcSel != RM_RET_OK)
        rc = m_dev->aiSetGestureCtrlIndividualR(0, on);
    const bool ok = (rc == RM_RET_OK || rcSel == RM_RET_OK);
    // Honest readback — rc=0 alone proved meaningless for gesture on this
    // device, so log what the camera says it actually did.
    bool devGesture = false, devTarget = false;
    if (m_dev->aiGetGestureParaR(Device::DevGestureParaTypeGesture, devGesture) == RM_RET_OK
        && m_dev->aiGetGestureParaR(Device::DevGestureParaTypeTargetSelection, devTarget) == RM_RET_OK)
        emit logLine("sys", QStringLiteral("gesture readback: function=%1, target-select=%2")
                                .arg(devGesture ? "on" : "off", devTarget ? "on" : "off"));
    emit commandResult(a, ok, rc, on ? QStringLiteral("on") : QStringLiteral("off"));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("gesture %1  rc=%2").arg(on ? "on" : "off").arg(rc));
}

void CameraWorker::cmdSetHdr(bool on) {
    const QString a = QStringLiteral("hdr");
    if (!requireDevice(a)) return;
    emit logLine("cmd", QStringLiteral("→ hdr %1").arg(on ? "on" : "off"));
    // DevWdrMode: None(0)=off, Dol2TO1(1)=HDR on.
    const int rc = m_dev->cameraSetWdrR(on ? 1 : 0);
    const bool ok = (rc == RM_RET_OK);
    emit commandResult(a, ok, rc, on ? QStringLiteral("on") : QStringLiteral("off"));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("hdr %1  rc=%2").arg(on ? "on" : "off").arg(rc));
}

void CameraWorker::cmdSetImage(const QString &param, int value) {
    if (!requireDevice(param)) return;
    const int v = value < 0 ? 0 : (value > 100 ? 100 : value);   // SDK range 0–100
    int rc = RM_RET_ERR;
    if (param == QLatin1String("brightness"))      rc = m_dev->cameraSetImageBrightnessR(v);
    else if (param == QLatin1String("contrast"))   rc = m_dev->cameraSetImageContrastR(v);
    else if (param == QLatin1String("saturation")) rc = m_dev->cameraSetImageSaturationR(v);
    else if (param == QLatin1String("sharpness"))  rc = m_dev->cameraSetImageSharpR(v);
    const bool ok = (rc == RM_RET_OK);
    emit commandResult(param, ok, rc, QString::number(v));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("%1 = %2  rc=%3").arg(param).arg(v).arg(rc));
}

void CameraWorker::cmdReadImageParams() {
    if (!m_dev) return;
    int b = 50, c = 50, s = 50, sh = 50;
    m_dev->cameraGetImageBrightnessR(b);
    m_dev->cameraGetImageContrastR(c);
    m_dev->cameraGetImageSaturationR(s);
    m_dev->cameraGetImageSharpR(sh);
    emit imageParams(b, c, s, sh);
}

void CameraWorker::cmdPresetCapture(int idx) {
    const QString a = QStringLiteral("preset %1 save").arg(idx + 1);
    if (!requireDevice(a)) return;
    emit logLine("cmd", QStringLiteral("→ %1").arg(a));
    float xyz[3] = {0, 0, 0};
    if (m_dev->gimbalGetAttitudeInfoR(xyz) != RM_RET_OK) {
        emit commandResult(a, false, -1, QStringLiteral("read attitude failed"));
        emit logLine("warn", a + QStringLiteral(": read attitude failed"));
        return;
    }
    float z = 1.0f;
    m_dev->cameraGetZoomAbsoluteR(z);   // best-effort; falls back to 1.0
    // fov = -1 → CameraController fills in its current FOV index.
    emit presetCaptured(idx, xyz[1], xyz[2], z, -1);
    emit commandResult(a, true, 0, QStringLiteral("captured pitch %1, yaw %2, %3x")
                                       .arg(xyz[1], 0, 'f', 1).arg(xyz[2], 0, 'f', 1).arg(z, 0, 'f', 2));
    emit logLine("ok", a + QStringLiteral(": captured"));
}

void CameraWorker::cmdPresetGo(int idx, double pitch, double yaw, double zoom, int fov, double speed) {
    const QString a = QStringLiteral("preset %1 go").arg(idx + 1);
    if (!requireDevice(a)) return;
    if (aiOwnsGimbal(a)) return;                 // CODE_REVIEW #4
    emit logLine("cmd", QStringLiteral("→ %1").arg(a));
    const float np = clampf(static_cast<float>(pitch), kPitchMin, kPitchMax);
    const float ny = clampf(static_cast<float>(yaw), kYawMin, kYawMax);
    const int rc = m_dev->gimbalSetSpeedPositionR(0.0f, np, ny, 0.0f,
                                                  static_cast<float>(speed), static_cast<float>(speed));
    if (fov >= 0)
        m_dev->cameraSetFovU(static_cast<Device::FovType>(fov));
    const float tz = clampf(static_cast<float>(zoom), 1.0f, 2.0f);
    m_dev->cameraSetZoomAbsoluteR(tz);
    refreshZoom();
    const bool ok = (rc == RM_RET_OK);
    emit commandResult(a, ok, rc,
                       QStringLiteral("pitch %1, yaw %2, %3x").arg(np, 0, 'f', 1).arg(ny, 0, 'f', 1).arg(tz, 0, 'f', 2));
    emit logLine(ok ? "ok" : "warn", QStringLiteral("%1  rc=%2").arg(a).arg(rc));
}

// ---------------------------------------------------------------------------
// PTZ velocity (hold-to-move) — see the header doc for the four safety stops.
// ---------------------------------------------------------------------------
void CameraWorker::cmdGimbalVelocity(double pitchSpeed, double yawSpeed) {
    if (!m_dev) return;   // silent: this is called on a repeating timer while dragging
    if (m_aiTracking) {
        // stop-on-(AI-owns-gimbal): honest, but only log once per drag, not per tick.
        if (!m_velocityBlockedLogged) {
            emit logLine("warn", QStringLiteral("ptz velocity: blocked — AI tracking owns the gimbal"));
            m_velocityBlockedLogged = true;
        }
        return;
    }
    m_velocityBlockedLogged = false;

    const int rc = m_dev->gimbalSpeedCtrlR(pitchSpeed, yawSpeed, 0.0);
    if (rc != RM_RET_OK) {
        // stop-on-error: never keep sending velocity after a rejected command.
        emit logLine("warn", QStringLiteral("ptz velocity: rc=%1 — stopping").arg(rc));
        m_dev->gimbalSpeedCtrlR(0.0, 0.0, 0.0);
        m_velocityActive = false;
        m_velocityWatchdog->stop();
        return;
    }
    if (!m_velocityActive) {
        m_velocityActive = true;
        emit logLine("cmd", QStringLiteral("→ ptz velocity: moving"));
    }
    m_velocityWatchdog->start();   // deadman: refreshed by every fresh command (restarts the interval)
}

void CameraWorker::cmdGimbalStop() {
    m_velocityWatchdog->stop();
    if (!m_velocityActive) return;   // avoid redundant calls/log spam
    m_velocityActive = false;
    if (m_dev) {
        const int rc = m_dev->gimbalSpeedCtrlR(0.0, 0.0, 0.0);
        emit logLine(rc == RM_RET_OK ? "ok" : "warn", QStringLiteral("ptz velocity: stop rc=%1").arg(rc));
    }
}

// ---------------------------------------------------------------------------
// Deterministic teardown (CODE_REVIEW #1/#2/#3/#9)
// ---------------------------------------------------------------------------
void CameraWorker::shutdown() {
    m_shuttingDown = true;
    if (m_pollTimer) m_pollTimer->stop();
    if (m_velocityActive && m_dev) m_dev->gimbalSpeedCtrlR(0.0, 0.0, 0.0);   // stop any in-flight motion
    if (m_velocityWatchdog) m_velocityWatchdog->stop();
    if (m_dev) {
        // Disable the status push BEFORE dropping the device so no further
        // sdkStatusTrampoline fires against state we are tearing down.
        m_dev->enableDevStatusCallback(false);
        m_dev.reset();
    }
    // Stop the SDK's discovery task and drop the plug/unplug callback.
    Devices::get().close();
    emit logLine("sys", QStringLiteral("shutdown: SDK released"));
}
