// CameraWorker — owns the OBSBOT SDK Device and runs EVERY blocking SDK call on
// a dedicated QThread. It never touches QML/GUI objects; it only emits Qt
// signals, which are delivered to the CameraController on the GUI thread via
// queued connections. This is the structural fix for the GTK PoC's shutdown
// UAF cluster (CODE_REVIEW #1/#2/#3/#9): there are no detached threads, the SDK
// status callback is disabled deterministically on shutdown(), and the worker
// outlives its own event loop so no queued event ever lands on a dead object.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

class Device;
class QTimer;

class CameraWorker : public QObject {
    Q_OBJECT
public:
    explicit CameraWorker(QObject *parent = nullptr);
    ~CameraWorker() override;

public slots:
    void init();                       // one-time setup, runs on the worker thread
    void startDiscovery(int waitMs);
    void rescan(int waitMs);

    void cmdWake();
    void cmdSleep();
    void cmdCenter();
    // dir: 0=up, 1=down, 2=left, 3=right. Bounded one-shot nudge.
    void cmdNudge(int dir, double stepDeg, double speed);
    void cmdZoom(double delta, bool absolute, double absVal, const QString &action);
    void cmdSetFov(int fovType, const QString &label);
    // AiWorkModeType (Human=2, PortraitTrack=14 — Tiny3-specific "face track" —
    // or None=0). subMode/from is always 0 here: the SDK's sub_mode_or_from
    // parameter of cameraSetAiModeU is documented "for tiny2" for the AiSubModeType
    // framing meaning, and 0 ("Normal case") is the universally-documented-safe
    // value for every other product/mode. See CameraController for why the
    // Framing (Normal/Upper/Close-up) control was removed on Tiny3.
    void cmdSetAi(int mode, int subMode, const QString &action);
    void cmdSetFace(bool on);                          // cameraSetFaceFocusR (independent)
    void cmdSetGesture(bool on);                       // aiSetGestureCtrlIndividualR
    void cmdSetHdr(bool on);                           // cameraSetWdrR
    void cmdSetImage(const QString &param, int value); // brightness/contrast/saturation/sharpness (0–100)
    void cmdReadImageParams();                         // read current image params on connect
    void cmdPresetCapture(int idx);
    void cmdPresetGo(int idx, double pitch, double yaw, double zoom, int fov, double speed);

    // VELOCITY (hold-to-move) PTZ — gated behind four safety stops, per the
    // design handoff: stop-on-release (caller stops sending on pointer-up),
    // stop-on-window-blur (CameraController::gimbalStop() wired to window
    // active-changed), stop-on-error (below: any non-OK rc auto-stops), and a
    // deadman timeout (m_velocityWatchdog — auto-stops if no fresh command
    // arrives within its interval, e.g. if the UI thread stalls).
    // pitchSpeed/yawSpeed are degrees/sec, matching gimbalSpeedCtrlR's units.
    void cmdGimbalVelocity(double pitchSpeed, double yawSpeed);
    void cmdGimbalStop();   // idempotent; safe to call anytime, incl. when not moving

    // Deterministic teardown: disable the status callback, drop the device, and
    // stop the SDK discovery task. Invoked BlockingQueued from the controller
    // before the thread is quit — see CameraController::~CameraController.
    void shutdown();

signals:
    void logLine(const QString &kind, const QString &message);
    void connectionResolved(bool found, const QString &product, const QString &sn,
                            const QString &fw, const QString &mode, int enumId);
    void deviceLost(const QString &reason);
    void statusUpdate(int runState, int aiModeRaw, double zoom, bool zoomValid);
    // Extra device state read from the same status push: face autofocus on/off,
    // HDR on/off, whether HDR is supported in the current mode, and current fps.
    void auxStatus(bool faceFocus, bool hdrOn, bool hdrSupport, int fps);
    void zoomUpdate(double zoom, bool valid);
    void imageParams(int brightness, int contrast, int saturation, int sharpness);
    void commandResult(const QString &action, bool ok, int rc, const QString &message);
    void presetCaptured(int idx, double pitch, double yaw, double zoom, int fov);

private:
    void pollTick();
    void bindDevice(const std::shared_ptr<Device> &d);
    bool requireDevice(const QString &action);
    // Returns true (and logs an honest "blocked" result) if AI tracking currently
    // owns the gimbal, so the caller must NOT issue a manual gimbal move.
    // CODE_REVIEW #4: never log a fake "(ok)" for a move the device will ignore.
    bool aiOwnsGimbal(const QString &action);
    void refreshZoom();
    void onSdkStatus(int runStatus, int aiMode, int faceFocus, int hdr, int hdrSupport, int fps);
    void onDevChanged(const QString &sn, bool plugged);

    static void sdkStatusTrampoline(void *param, const void *data);

    std::shared_ptr<Device> m_dev;
    QString m_sn;
    QTimer *m_pollTimer = nullptr;
    int m_pollElapsedMs = 0;
    int m_pollTimeoutMs = 6000;
    bool m_devChangedRegistered = false;
    std::atomic<bool> m_shuttingDown{false};

    // Cached from the SDK status push; used only for the honest AI-owns-gimbal
    // guard. Never causes an unwanted move.
    bool m_aiTracking = false;
    // Grace window after a CONFIRMED "AI off" command: the device's status push
    // lags the command by a full push cycle (2–3 s), so a stale push still
    // reporting an AI mode would flip m_aiTracking back on and reject the very
    // moves users expect right after turning AI off (Rex's hardware finding:
    // the automatic "return to preset after AI off" always landed in this
    // window and got blocked/truncated). While the window is open, pushes
    // claiming AI-on are treated as stale.
    QElapsedTimer m_aiOffGrace;

    // Velocity-mode state (hold-to-move PTZ).
    bool m_velocityActive = false;         // true while a hold-drag is in progress
    bool m_velocityBlockedLogged = false;  // log "blocked by AI" once per drag, not per tick
    QTimer *m_velocityWatchdog = nullptr;  // deadman: auto-stop if no fresh command arrives
};
