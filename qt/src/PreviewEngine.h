// PreviewEngine — embedded live preview (GUI thread, Qt Multimedia).
//
// Renders the camera's real UVC video stream into a QML VideoOutput inside the
// app window (issue #1; replaces the external ffplay window). This is a plain
// V4L2/UVC capture — completely independent of the OBSBOT SDK control channel
// (different USB interface), so PTZ/AI control keeps working while streaming.
//
// Honesty rules carried over from the rest of the app:
//   * The OBSBOT video node is selected BY DEVICE NAME from QMediaDevices —
//     never a hardcoded /dev/video0, never "whatever camera is first".
//   * The requested format is a REAL MJPG mode the device advertises
//     (PreviewFormats.h); if the exact mode is missing we log the fallback.
//   * Capture errors (device busy, unplug) stop the preview and are reported
//     in the activity log — no frozen last-frame pretending to be live.
#pragma once

#include <QCamera>
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QObject>
#include <QPointer>
#include <QVideoSink>

#include <memory>

class PreviewEngine : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // The QML VideoOutput's sink; assigned once from Viewfinder.qml.
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

public:
    explicit PreviewEngine(QObject *parent = nullptr);
    ~PreviewEngine() override;

    bool available() const { return !m_device.isNull(); }
    QString unavailableReason() const;
    bool active() const { return m_active; }
    QVideoSink *videoSink() const { return m_sink; }
    void setVideoSink(QVideoSink *sink);

public slots:
    void start();
    void stop();
    void toggle();
    // Same index space as AppSettings::previewResIndex / kPreviewRes. If the
    // preview is running, restarts it — a UVC mode change needs renegotiation.
    void setResIndex(int idx);

signals:
    void availabilityChanged();
    void activeChanged();
    void videoSinkChanged();
    // Same (kind, message) shape as CameraController::logLine; main.cpp chains
    // this into the controller's signal so preview events land in the one log.
    void logLine(const QString &kind, const QString &message);

private:
    void refreshDevice();               // (re)pick the OBSBOT node; handles hotplug
    QCameraFormat pickFormat();         // exact MJPG mode, or closest honest fallback (logs)

    QMediaDevices m_mediaDevices;       // emits videoInputsChanged on hotplug
    QCameraDevice m_device;             // the matched OBSBOT capture device
    QMediaCaptureSession m_session;
    std::unique_ptr<QCamera> m_camera;  // created per start(), destroyed on stop()
    QPointer<QVideoSink> m_sink;
    bool m_active = false;
    int m_resIndex = 1;                 // synced from settings by main.cpp
};
