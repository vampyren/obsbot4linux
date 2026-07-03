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
    // Writes BOTH gesture config stores + full readback. lowTraffic gates the
    // experimental 15 s status cadence (opt-in setting — see setGestureFriendly).
    void cmdSetGesture(bool on, bool lowTraffic);
    void cmdSetHdr(bool on);                           // cameraSetWdrR
    // Auto-sleep timer (cameraSetSuspendTimeU; <=0 disables auto-sleep) and
    // mic-during-sleep (cameraSetMicrophoneDuringSleepU). The SDK category
    // docs omit the Tiny 3 for BOTH — rc + the status push's sleep_micro
    // readback are the honest verdict (gesture taught us rc alone can lie).
    void cmdSetAutoSleep(int seconds, const QString &label);
    void cmdSetMicSleep(bool on);
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

    // GESTURE DIAGNOSTIC (hardware finding: the Tiny 3 executes gestures
    // autonomously when NO app session is attached, but goes gesture-deaf while
    // this app runs). Quiet mode silences ALL periodic SDK traffic (status push
    // subscription + the per-push zoom getter) for `seconds` while staying
    // attached, to determine whether the traffic or the session itself
    // suppresses the recognizer. HARDWARE-CONFIRMED (Rex, quiet-test): it's the
    // TRAFFIC — palm gestures worked during the quiet window.
    void cmdQuietMode(int seconds);

    // The permanent fix built on that finding: while gesture control is ON,
    // drop the status cadence from the SDK's ~2–3 s push to a one-shot
    // enable→push→disable duty cycle every kStatusDutyMs, leaving the USB
    // control channel quiet enough for the camera's recognizer to work.
    // Applied automatically by cmdSetGesture on success.
    void setGestureFriendly(bool on);

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
    // sleepMicro: device-reported mic-during-sleep flag (0/1) from the push —
    // the readback for cmdSetMicSleep.
    void auxStatus(bool faceFocus, bool hdrOn, bool hdrSupport, int fps, int sleepMicro);
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
    // Gesture-friendly mode: open a one-push status window NOW (closed again by
    // onSdkStatus). Called after state-changing commands (wake/sleep/AI/preset)
    // so their effects reach the UI immediately instead of at the next duty
    // tick — e.g. the wake edge must not fire the startup preset 15 s late.
    // The command itself just made traffic, so this pulse costs nothing extra
    // with respect to recognizer suppression.
    void statusPulse();
    void onSdkStatus(int runStatus, int aiMode, int faceFocus, int hdr, int hdrSupport, int fps, int sleepMicro);
    void onDevChanged(const QString &sn, bool plugged);

    static void sdkStatusTrampoline(void *param, const void *data);

    std::shared_ptr<Device> m_dev;
    QString m_sn;
    QTimer *m_pollTimer = nullptr;
    int m_pollElapsedMs = 0;
    int m_pollTimeoutMs = 6000;
    bool m_devChangedRegistered = false;
    std::atomic<bool> m_shuttingDown{false};
    bool m_quiet = false;   // gesture diagnostic: drop status pushes while true

    // Gesture-friendly status cadence (see setGestureFriendly).
    bool m_gestureFriendly = false;
    bool m_awaitingDutyPush = false;   // duty window open, waiting for one push
    QTimer *m_statusDutyTimer = nullptr;

    // Cached from the SDK status push; used only for the honest AI-owns-gimbal
    // guard. Never causes an unwanted move.
    bool m_aiTracking = false;
    // Grace windows after a CONFIRMED AI command (see onSdkStatus): each
    // swallows exactly ONE stale contradicting status push — the device lags a
    // command by up to a push cycle — then closes, so a genuine device-side
    // change (e.g. a palm gesture re-engaging tracking) is at most one push
    // late instead of being rewritten for the whole window.
    QElapsedTimer m_aiOffGrace;   // after AI-off: swallow one stale "on" push
    QElapsedTimer m_aiOnGrace;    // after AI-on: swallow one stale "None" push
    int m_lastAiOnMode = 0;       // the mode we commanded on (forwarded during on-grace)

    // Velocity-mode state (hold-to-move PTZ).
    bool m_velocityActive = false;         // true while a hold-drag is in progress
    bool m_velocityBlockedLogged = false;  // log "blocked by AI" once per drag, not per tick
    QTimer *m_velocityWatchdog = nullptr;  // deadman: auto-stop if no fresh command arrives
};
