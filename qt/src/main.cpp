// OBSBOT Tiny 3 Command Center — Qt 6 / QML entry point.
//
// GUI mode: loads the QML "Command Center" UI and starts USB discovery off the
// UI thread (via CameraController's worker QThread).
//
// --self-test: runs headless (offscreen platform), performs discovery once, and
// exits with a code that reflects the DEVICE result only:
//     0 = device found, 3 = no device by timeout, 2 = app/init error.
// (CODE_REVIEW #11: a headless run no longer returns 0 for "no device".)
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "CameraController.h"

int main(int argc, char **argv) {
    int waitMs = 6000;
    bool selfTest = false;

    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--self-test") {
            selfTest = true;
        } else if (a == "--wait-ms" && i + 1 < argc) {
            waitMs = std::max(0, std::atoi(argv[++i]));
        } else if (a == "-h" || a == "--help") {
            std::printf("Usage: %s [--wait-ms N] [--self-test]\n", argv[0]);
            std::printf("  --wait-ms N   USB discovery timeout in ms (default 6000)\n");
            std::printf("  --self-test   Run discovery once headless, then exit\n");
            std::printf("                (exit 0 = device found, 3 = none, 2 = init error)\n");
            return 0;
        }
    }

    // Headless self-test must not require a display server (override whatever the
    // session / AppImage set, e.g. xcb).
    if (selfTest) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    } else if (qEnvironmentVariableIsSet("APPDIR") || qEnvironmentVariableIsSet("APPIMAGE")) {
        // Running from the AppImage, which bundles only the xcb platform plugin.
        // A Wayland session that exports QT_QPA_PLATFORM=wayland would make Qt
        // abort (no wayland plugin bundled), so steer to xcb — it runs fine on
        // Wayland via XWayland. Override with OBSBOT_QT_PLATFORM=… if desired.
        // (Gated on APPDIR/APPIMAGE so a normal source build can still go native.)
        const QByteArray override = qgetenv("OBSBOT_QT_PLATFORM");
        const QByteArray current = qgetenv("QT_QPA_PLATFORM");
        if (!override.isEmpty())
            qputenv("QT_QPA_PLATFORM", override);
        else if (current.isEmpty() || current.contains("wayland"))
            qputenv("QT_QPA_PLATFORM", "xcb");
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("OBSBOT4Linux");
    QGuiApplication::setOrganizationName("obsbot4linux");

    CameraController controller;

    if (selfTest) {
        int code = 3;   // default: no device
        QObject::connect(&controller, &CameraController::logLine, &app,
                         [](const QString &k, const QString &m) {
                             std::fprintf(stderr, "[%s] %s\n", qPrintable(k), qPrintable(m));
                         });
        QObject::connect(&controller, &CameraController::discoveryFinished, &app,
                         [&](bool found) {
                             if (found) {
                                 code = 0;
                                 std::fprintf(stdout,
                                     "[self-test] device: FOUND product=%s SN=%s fw=%s enum=%d\n",
                                     qPrintable(controller.property("product").toString()),
                                     qPrintable(controller.property("sn").toString()),
                                     qPrintable(controller.property("firmware").toString()),
                                     controller.property("enumId").toInt());
                             } else {
                                 std::fprintf(stdout, "[self-test] device: NO DEVICE (timeout %d ms)\n", waitMs);
                             }
                             app.quit();
                         });
        // Safety net if discovery never reports back.
        QTimer::singleShot(waitMs + 3000, &app, [&]() {
            std::fprintf(stderr, "[self-test] safety timeout — discovery did not report back\n");
            app.quit();
        });
        std::fprintf(stdout, "[self-test] UI backend: %s (offscreen headless)\n",
                     qPrintable(QGuiApplication::platformName()));
        controller.start(waitMs);
        app.exec();
        return code;
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("cam", &controller);
    engine.loadFromModule("Obsbot", "Main");
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "Failed to load QML UI (no display, or missing Qt Quick runtime).\n");
        return 2;
    }

    controller.start(waitMs);
    return app.exec();
}
