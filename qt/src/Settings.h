// Repo-local settings + preset persistence for the OBSBOT Tiny 3 Qt app.
//
// Stored as a single JSON document at ./config/obsbot4linux.json
// (relative to the process CWD; the launcher cd's to the repo root). The path
// can be overridden with the OBSBOT4LINUX_CONFIG environment variable.
//
// This is a plain value store — no SDK, no Qt GUI dependency beyond QtCore — so
// it is trivially testable and safe to touch from the GUI thread only.
#pragma once

#include <QJsonObject>
#include <QString>
#include <array>

struct PresetData {
    bool set = false;
    QString name;
    double pan = 0.0;   // yaw   (degrees)
    double tilt = 0.0;  // pitch (degrees)
    double zoom = 1.0;  // 1.0–2.0
    int fov = 0;        // FovType index (0=86,1=78,2=65), -1 = unknown/device
};

// One flat, serialisable settings blob. Defaults here are the app defaults used
// on first run (no config file yet).
struct AppSettings {
    // Move / transport defaults.
    int  moveStepDeg = 5;    // 2 / 5 / 10
    int  speedMode   = 0;    // 0=Slow, 1=Medium
    int  fovIndex    = 0;    // 0=Wide86, 1=Medium78, 2=Narrow65

    // Go to this preset automatically when the device connects. 0=none, 1..3=Pn.
    // Explicit opt-in; this is a deliberate move the user chose (never a surprise).
    int  startupPreset = 0;

    // Go to this preset automatically when AI tracking is turned OFF. 0=none,
    // 1..3=Pn. Explicit opt-in (a deliberate move the user chose).
    int  aiReturnPreset = 0;

    // Preview (ffplay) capture resolution/framerate. Index into a fixed list —
    // see CameraController::previewResString(). This is the resolution ffplay
    // requests from /dev/video0, NOT an SDK device setting.
    int  previewResIndex = 1;   // default 1080p60

    // Put the camera to sleep when the app closes. Off by default.
    bool sleepOnExit = false;

    // Presets (app-local; the SDK's native presets are undocumented for Tiny 3).
    std::array<PresetData, 3> presets{};

    // Image / tracking config values are persisted so the UI restores, but note
    // they are only *applied* to the device when the control is capability-gated
    // ON (see CameraController capability flags). Persisting the value is honest
    // even when the control is disabled.
    int  brightness = 50, contrast = 50, saturation = 50, sharpen = 50;
    int  wbMode = 0;          // 0=Auto,1=Manual
    int  wbTemp = 5000;       // 2800–7500 K
    int  exposure = 0;        // -1.0..+1.0 EV encoded as tenths (-10..+10)
    bool hdr = false;

    int  trackFraming = 1;    // 0=Close,1=Upper,2=Full
    int  trackSpeed  = 1;     // 0=Slow,1=Std,2=Fast
    int  trackZone   = 0;     // 0=Auto,1=Center,2=Left,3=Right
    int  sensitivity = 50;
    bool gesture = false;
};

class Settings {
public:
    // Resolve the config file path (env override, else ./config/...).
    static QString configPath();

    // Load from disk; returns defaults if the file is absent or unreadable.
    static AppSettings load();

    // Persist to disk (creates the config dir if needed). Returns false on I/O
    // failure (caller logs it — persistence failure must be visible, not silent).
    static bool save(const AppSettings &s);

    // JSON (de)serialisation, exposed for the settings unit test.
    static QJsonObject toJson(const AppSettings &s);
    static AppSettings fromJson(const QJsonObject &o);
};
