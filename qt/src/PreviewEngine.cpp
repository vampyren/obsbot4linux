#include "PreviewEngine.h"
#include "JpegDht.h"
#include "PreviewFormats.h"

#include <QDir>
#include <QImage>
#include <QVideoFrame>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

// ioctl with EINTR retry (signals can interrupt V4L2 calls).
int xioctl(int fd, unsigned long req, void *arg) {
    int r;
    do { r = ::ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

constexpr int kBufCount = 4;          // mmap ring size
constexpr int kPollTimeoutMs = 3000;  // no frame for this long = stalled stream
constexpr int kMaxDecodeFailures = 30; // consecutive bad JPEGs before giving up

} // namespace

// ---------------------------------------------------------------------------
// V4l2CaptureThread — the blocking capture loop
// ---------------------------------------------------------------------------
V4l2CaptureThread::V4l2CaptureThread() {
    // Self-pipe so requestStop() can wake a poll() that is waiting for a frame —
    // without it, stopping a stalled stream would block for the full poll
    // timeout (and a bounded join could time out, see teardownThread).
    if (::pipe2(m_wake, O_CLOEXEC | O_NONBLOCK) != 0)
        m_wake[0] = m_wake[1] = -1;   // degraded: stop still works via timeout
}

V4l2CaptureThread::~V4l2CaptureThread() {
    if (m_wake[0] >= 0) ::close(m_wake[0]);
    if (m_wake[1] >= 0) ::close(m_wake[1]);
}

void V4l2CaptureThread::configure(const QString &devPath, int w, int h, int fps,
                                  QVideoSink *sink) {
    m_devPath = devPath;
    m_w = w; m_h = h; m_fps = fps;
    setSink(sink);
    m_stop.store(false);
}

void V4l2CaptureThread::setSink(QVideoSink *sink) {
    QMutexLocker lock(&m_sinkMutex);
    m_sink = sink;
}

void V4l2CaptureThread::requestStop() {
    m_stop.store(true);
    if (m_wake[1] >= 0) {
        const char b = 'x';
        [[maybe_unused]] ssize_t n = ::write(m_wake[1], &b, 1);
    }
}

void V4l2CaptureThread::run() {
    const QByteArray path = m_devPath.toLocal8Bit();
    const int fd = ::open(path.constData(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        emit failed(QStringLiteral("cannot open %1: %2").arg(m_devPath,
                        QString::fromLocal8Bit(std::strerror(errno))));
        emit stopped();
        return;
    }
    // Note: uvcvideo allows multiple open()s — "another app is streaming"
    // surfaces as EBUSY at S_FMT/REQBUFS below, not here.

    struct Guard {                    // cleanup on every exit path
        int fd;
        void *maps[kBufCount] = {};
        size_t lens[kBufCount] = {};
        bool streaming = false;
        ~Guard() {
            if (streaming) {
                v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                xioctl(fd, VIDIOC_STREAMOFF, &t);
            }
            for (int i = 0; i < kBufCount; ++i)
                if (maps[i]) ::munmap(maps[i], lens[i]);
            ::close(fd);
        }
    } guard{fd};

    // Format: MJPG at the requested size. The driver adjusts if unsupported —
    // we read back and report what it really set.
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = static_cast<__u32>(m_w);
    fmt.fmt.pix.height = static_cast<__u32>(m_h);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        emit failed(errno == EBUSY
                        ? QStringLiteral("device busy — another app is using the camera")
                        : QStringLiteral("S_FMT failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        emit stopped();
        return;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        emit failed(QStringLiteral("device did not accept MJPG for %1x%2").arg(m_w).arg(m_h));
        emit stopped();
        return;
    }

    // Frame rate: the exact interval — THE fix for Qt's stream-at-max behavior.
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe = {1, static_cast<__u32>(m_fps)};
    xioctl(fd, VIDIOC_S_PARM, &parm);   // best-effort; readback below is the truth
    parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    double actualFps = 0.0;
    if (xioctl(fd, VIDIOC_G_PARM, &parm) == 0 && parm.parm.capture.timeperframe.numerator)
        actualFps = double(parm.parm.capture.timeperframe.denominator)
                    / double(parm.parm.capture.timeperframe.numerator);

    // mmap ring
    v4l2_requestbuffers req{};
    req.count = kBufCount;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
        emit failed(errno == EBUSY
                        ? QStringLiteral("device busy — another app is using the camera")
                        : QStringLiteral("REQBUFS failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        emit stopped();
        return;
    }
    if (req.count < 2) {   // ioctl OK but driver granted too few buffers — no errno here
        emit failed(QStringLiteral("REQBUFS granted only %1 buffer(s)").arg(req.count));
        emit stopped();
        return;
    }
    const int nbufs = static_cast<int>(req.count) < kBufCount ? static_cast<int>(req.count) : kBufCount;
    for (int i = 0; i < nbufs; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = static_cast<__u32>(i);
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) == -1) { emit failed(QStringLiteral("QUERYBUF failed")); emit stopped(); return; }
        guard.maps[i] = ::mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
        guard.lens[i] = b.length;
        if (guard.maps[i] == MAP_FAILED) { guard.maps[i] = nullptr; emit failed(QStringLiteral("mmap failed")); emit stopped(); return; }
        if (xioctl(fd, VIDIOC_QBUF, &b) == -1) { emit failed(QStringLiteral("QBUF failed")); emit stopped(); return; }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        emit failed(QStringLiteral("STREAMON failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        emit stopped();
        return;
    }
    guard.streaming = true;

    emit negotiated(static_cast<int>(fmt.fmt.pix.width), static_cast<int>(fmt.fmt.pix.height), actualFps);

    int decodeFailures = 0;
    bool dhtLogged = false;
    while (!m_stop.load()) {
        pollfd pfds[2] = {{fd, POLLIN, 0}, {m_wake[0], POLLIN, 0}};
        const int npfd = (m_wake[0] >= 0) ? 2 : 1;
        const int pr = ::poll(pfds, static_cast<nfds_t>(npfd), kPollTimeoutMs);
        if (m_stop.load()) break;   // requestStop() wrote the wake pipe (or flag raced poll)
        if (pr <= 0) {
            emit failed(pr == 0 ? QStringLiteral("stream stalled (no frame for %1 ms)").arg(kPollTimeoutMs)
                                : QStringLiteral("poll error: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
            break;
        }
        // Skip ONLY pure self-pipe wakeups. Device errors (unplug) surface as
        // POLLERR/POLLHUP on the device fd, usually WITHOUT POLLIN — those must
        // fall through to DQBUF so the real errno (ENODEV) is reported; a bare
        // `continue` would busy-spin at 100% CPU (poll returns instantly with
        // POLLERR forever) with the preview silently frozen.
        if (!(pfds[0].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)))
            continue;                                // woken by the pipe, not the device
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &b) == -1) {
            if (errno == EAGAIN) continue;
            emit failed(QStringLiteral("DQBUF failed: %1 (device unplugged?)")
                            .arg(QString::fromLocal8Bit(std::strerror(errno))));
            break;
        }

        // Decode the JPEG frame (Qt's libjpeg plugin) and hand it to the sink.
        // Many UVC cams emit MJPG without Huffman tables — libjpeg fails on
        // those where ffmpeg silently fixes them up, so on failure retry with
        // the standard tables spliced in (JpegDht.h). Occasional broken frames
        // are normal — skip them, but give up honestly if NOTHING decodes.
        const auto *data = static_cast<const uchar *>(guard.maps[b.index]);
        QImage img = QImage::fromData(data, static_cast<int>(b.bytesused), "JPG");
        if (img.isNull()) {
            const QByteArray fixed = withStandardDht(data, static_cast<int>(b.bytesused));
            if (!fixed.isEmpty()) {
                img = QImage::fromData(fixed, "JPG");
                if (!img.isNull() && !dhtLogged) {
                    dhtLogged = true;
                    emit negotiatedDht();
                }
            }
        }
        if (!img.isNull()) {
            decodeFailures = 0;
            // Deliver WITH the mutex held: PreviewEngine clears the sink through
            // this same mutex before the sink can be destroyed, so this cannot
            // race the sink's destruction (setVideoFrame itself is thread-safe).
            QMutexLocker lock(&m_sinkMutex);
            if (QVideoSink *sink = m_sink.data())
                sink->setVideoFrame(QVideoFrame(img));
        } else if (++decodeFailures >= kMaxDecodeFailures) {
            emit failed(QStringLiteral("cannot decode the MJPG stream (%1 bad frames in a row)")
                            .arg(decodeFailures));
            xioctl(fd, VIDIOC_QBUF, &b);
            break;
        }
        xioctl(fd, VIDIOC_QBUF, &b);
    }
    emit stopped();
}

// ---------------------------------------------------------------------------
// PreviewEngine
// ---------------------------------------------------------------------------
PreviewEngine::PreviewEngine(QObject *parent) : QObject(parent) {
    // QMediaDevices is used ONLY as a hotplug trigger; the actual node scan is
    // ours (QUERYCAP card name), so no dependency on Qt's format enumeration.
    connect(&m_mediaDevices, &QMediaDevices::videoInputsChanged,
            this, &PreviewEngine::refreshDevice);
    refreshDevice();
}

PreviewEngine::~PreviewEngine() {
    teardownThread();
}

QString PreviewEngine::unavailableReason() const {
    if (available()) return QString();
    return QStringLiteral("No OBSBOT video device found — preview needs the camera's UVC node.");
}

void PreviewEngine::setVideoSink(QVideoSink *sink) {
    if (sink == m_sink) return;
    m_sink = sink;
    if (m_thread)
        m_thread->setSink(sink);   // live re-target (page switch while streaming)
    emit videoSinkChanged();
}

void PreviewEngine::refreshDevice() {
    // Scan /dev/video* and pick the first CAPTURE node whose driver-reported
    // card name contains "OBSBOT". Metadata nodes (e.g. /dev/video1) report the
    // same card but lack V4L2_CAP_VIDEO_CAPTURE, so they're skipped naturally.
    const bool wasAvailable = available();
    const QString oldPath = m_devPath;
    QString found;

    QStringList nodes = QDir(QStringLiteral("/dev"))
                            .entryList({QStringLiteral("video*")}, QDir::System | QDir::Files);
    // Numeric order (lexical puts video10 before video2) so the pick is stable
    // across boots when several nodes exist.
    std::sort(nodes.begin(), nodes.end(), [](const QString &a, const QString &b) {
        return a.mid(5).toInt() < b.mid(5).toInt();
    });
    for (const QString &n : nodes) {
        const QString path = QStringLiteral("/dev/") + n;
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        v4l2_capability cap{};
        const bool ok = (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0);
        ::close(fd);
        if (!ok) continue;
        const __u32 caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps
                                                                     : cap.capabilities;
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE)) continue;
        const QString card = QString::fromLatin1(reinterpret_cast<const char *>(cap.card));
        if (!card.contains(QLatin1String("OBSBOT"), Qt::CaseInsensitive)) continue;
        found = path;
        break;
    }

    if (found == oldPath) return;   // no change (incl. both empty)
    m_devPath = found;

    if (m_active && found.isEmpty()) {
        stop();
        emit logLine("err", QStringLiteral("preview: video device lost — stopped"));
    } else if (m_active && found != oldPath) {
        // Re-enumerated under a new node while streaming: the old fd is dead.
        stop();
        emit logLine("err", QStringLiteral("preview: video device changed — stopped"));
    }
    emit availabilityChanged();
    if (available() && !wasAvailable)
        emit logLine("sys", QStringLiteral("preview: video device found (%1)").arg(m_devPath));
}

void PreviewEngine::teardownThread() {
    if (!m_thread) return;
    V4l2CaptureThread *t = m_thread;
    m_thread = nullptr;
    ++m_generation;                 // queued signals from this thread are now stale
    t->disconnect(this);
    // Clear the sink FIRST, through the thread's mutex: after this returns the
    // capture loop can never dereference the (possibly about-to-die) sink, even
    // in the pathological leaked-thread case below.
    t->setSink(nullptr);
    t->requestStop();               // sets the flag AND wakes poll() via the self-pipe
    t->wait(4000);                  // > kPollTimeoutMs so the bound holds even with no pipe
    if (t->isRunning()) {           // pathological: don't block the GUI forever
        connect(t, &QThread::finished, t, &QObject::deleteLater);
    } else {
        delete t;
    }
}

void PreviewEngine::start() {
    if (m_active || m_thread) return;
    if (!available()) {
        emit logLine("warn", unavailableReason());
        return;
    }

    const int ri = (m_resIndex >= 0 && m_resIndex < kPreviewResCount) ? m_resIndex : 0;
    const PreviewRes &want = kPreviewRes[ri];

    m_thread = new V4l2CaptureThread;
    m_thread->configure(m_devPath, want.w, want.h, want.fps, m_sink);

    const quint64 gen = ++m_generation;
    connect(m_thread, &V4l2CaptureThread::negotiated, this,
            [this, gen](int w, int h, double fps) {
                if (gen != m_generation) return;
                // Honest: only claim a rate the driver actually reported back.
                emit logLine("ok", fps > 0.0
                    ? QStringLiteral("preview: streaming %1x%2 @ %3 fps (driver-confirmed)")
                          .arg(w).arg(h).arg(fps, 0, 'f', 0)
                    : QStringLiteral("preview: streaming %1x%2 (driver did not report a rate)")
                          .arg(w).arg(h));
                if (!m_active) { m_active = true; emit activeChanged(); }
            }, Qt::QueuedConnection);
    connect(m_thread, &V4l2CaptureThread::negotiatedDht, this,
            [this, gen]() {
                if (gen != m_generation) return;
                emit logLine("sys", QStringLiteral(
                    "preview: frames lack Huffman tables (DHT) — splicing in the standard tables"));
            }, Qt::QueuedConnection);
    connect(m_thread, &V4l2CaptureThread::failed, this,
            [this, gen](const QString &why) {
                if (gen != m_generation) return;
                emit logLine("err", QStringLiteral("preview: %1").arg(why));
                stop();
            }, Qt::QueuedConnection);

    m_thread->start();
    emit logLine("cmd", QStringLiteral("preview: starting embedded stream at %1 (MJPG, direct V4L2)")
                            .arg(QLatin1String(want.label)));
}

void PreviewEngine::stop() {
    teardownThread();
    if (m_active) {
        m_active = false;
        emit activeChanged();
        emit logLine("sys", QStringLiteral("preview: stopped"));
    }
}

void PreviewEngine::toggle() {
    if (m_active || m_thread) stop();
    else start();
}

void PreviewEngine::setResIndex(int idx) {
    if (idx < 0 || idx >= kPreviewResCount || idx == m_resIndex) return;
    m_resIndex = idx;
    // A UVC mode change needs stream renegotiation — restart if running.
    if (m_thread) {
        stop();
        start();
    }
}
