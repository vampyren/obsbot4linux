// PreviewEngine — embedded live preview (direct V4L2 capture → QML VideoOutput).
//
// v0.2.x history: the first implementation used QCamera (Qt Multimedia), but
// Qt's ffmpeg backend collapses the camera's discrete frame rates into ONE
// min–max range per resolution and then always streams at the MAX — "1080p30"
// came out as 1080p120 on hardware (hardware-verified: the negotiated-format
// log showed "15–120 fps" and the SDK reported fps=120). 120 fps quarters the
// per-frame exposure, which visibly shifts colors and softens the image. Qt's
// public API offers no way to choose a rate inside the range, so this engine
// now drives V4L2 directly (same ioctls ffplay uses): exact MJPG mode, exact
// frame rate, honest G_FMT/G_PARM readback of what the driver really set.
//
// Honesty rules:
//   * The OBSBOT node is found by driver-reported card name (VIDIOC_QUERYCAP)
//     — never a hardcoded /dev/video0, never "whatever camera is first".
//   * The NEGOTIATED format/rate is logged from driver readback, not assumed.
//   * Capture errors (device busy, unplug, decode failure, stall) stop the
//     preview with the reason in the activity log — no frozen "live" frame.
#pragma once

#include <QMediaDevices>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QVideoSink>

#include <atomic>

// Blocking V4L2 MJPG capture loop on its own thread: open → S_FMT → S_PARM →
// mmap ring → STREAMON → DQBUF/decode/deliver until stopped. Emits queued
// signals only; the QVideoSink is delivered frames via the thread-safe
// QVideoSink::setVideoFrame.
class V4l2CaptureThread : public QThread {
    Q_OBJECT
public:
    V4l2CaptureThread();
    ~V4l2CaptureThread() override;
    void configure(const QString &devPath, int w, int h, int fps, QVideoSink *sink);
    void setSink(QVideoSink *sink);              // thread-safe live re-target
    void requestStop();                          // sets the flag AND wakes poll()

signals:
    void negotiated(int w, int h, double fps);   // driver readback after STREAMON (fps<=0 = unknown)
    void negotiatedDht();                        // frames lack DHT — standard tables spliced (once)
    void failed(const QString &why);             // open/ioctl/decode/stall error
    void stopped();                              // capture loop exited (any reason)

protected:
    void run() override;

private:
    QString m_devPath;
    int m_w = 1920, m_h = 1080, m_fps = 30;
    // The sink is only ever dereferenced WITH this mutex held (the frame-deliver
    // call included), and PreviewEngine clears it through the same mutex before
    // the sink object can be destroyed — so the capture loop can never touch a
    // dying sink, even if a pathological join timeout leaves the loop running.
    QMutex m_sinkMutex;
    QPointer<QVideoSink> m_sink;
    std::atomic<bool> m_stop{false};
    int m_wake[2] = {-1, -1};                    // self-pipe: requestStop() wakes poll()
};

class PreviewEngine : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // The QML VideoOutput's sink; the on-screen Viewfinder claims it.
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

public:
    explicit PreviewEngine(QObject *parent = nullptr);
    ~PreviewEngine() override;

    bool available() const { return !m_devPath.isEmpty(); }
    QString unavailableReason() const;
    bool active() const { return m_active; }
    QVideoSink *videoSink() const { return m_sink; }
    void setVideoSink(QVideoSink *sink);

public slots:
    void start();
    void stop();
    void toggle();
    // Same index space as AppSettings::previewResIndex / kPreviewRes. Restarts
    // a running stream (a UVC mode change needs renegotiation).
    void setResIndex(int idx);

signals:
    void availabilityChanged();
    void activeChanged();
    void videoSinkChanged();
    // Same (kind, message) shape as CameraController::logLine; main.cpp chains
    // this into the controller's signal so preview events land in the one log.
    void logLine(const QString &kind, const QString &message);

private:
    void refreshDevice();            // (re)scan /dev/video* for the OBSBOT node
    void teardownThread();           // stop + join the capture thread

    QMediaDevices m_mediaDevices;    // used ONLY as a hotplug change trigger
    QString m_devPath;               // e.g. "/dev/video0" (found by card name)
    V4l2CaptureThread *m_thread = nullptr;
    QPointer<QVideoSink> m_sink;
    bool m_active = false;
    int m_resIndex = 1;              // synced from settings by main.cpp
    quint64 m_generation = 0;        // stale queued signals from old threads bail
};
