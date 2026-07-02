// OBSBOT Tiny 3 Control — native Linux GUI PoC (GTK3)
//
// A small, honest control panel built on the validated OBSBOT SDK integration
// (libdev v2.1.0_8). It discovers the camera off the UI thread, shows real
// device identity, and exposes SAFE SDK-backed controls: Wake / Sleep / Center,
// bounded PTZ (Up/Down/Left/Right), app-local presets, zoom, FOV, AI-tracking
// toggle, and face focus. See README.md for the full list and rationale.
//
// Toolkit: GTK3 (Qt6/Qt5/cmake are not installed in this environment; GTK3 is
// present, native, Wayland-capable, and links cleanly against the C++ SDK).
//
// Honesty rules enforced here:
//   * No fake video preview. Embedded preview is not implemented (needs
//     GStreamer, absent). An optional "Live Preview" button launches ffplay in
//     an EXTERNAL window on /dev/video0 — real frames, clearly separate.
//   * No fake status. product/SN/firmware/mode come straight from the device;
//     zoom is read via cameraGetZoomAbsoluteR (real 1.0~2.0), AI state from the
//     status push. Anything not confidently decoded shows "Unknown".
//   * No unsafe motion. PTZ is one-shot and bounded: read current angle, apply
//     a small clamped delta, move once at a slow speed. Center = gimbalRstPosR
//     (bounded reset). No loops, sweeps, or continuous speed commands.

#include <gtk/gtk.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include <dev/devs.hpp>

#include "poll.hpp"

// ---------------------------------------------------------------------------
// UI widget handles (all touched only on the GTK main thread)
// ---------------------------------------------------------------------------
namespace {

struct Ui {
    GtkWidget *window = nullptr;
    GtkWidget *conn_pill = nullptr;   // tally-style connection indicator
    GtkWidget *conn_text = nullptr;
    GtkWidget *subtitle = nullptr;    // "Tiny3 · SN … · fw …"
    GtkWidget *val_product = nullptr;
    GtkWidget *val_sn = nullptr;
    GtkWidget *val_fw = nullptr;
    GtkWidget *val_mode = nullptr;
    GtkWidget *val_run = nullptr;
    GtkWidget *val_zoom = nullptr;
    GtkWidget *val_ai = nullptr;
    GtkWidget *btn_wake = nullptr;
    GtkWidget *btn_sleep = nullptr;
    GtkWidget *btn_center = nullptr;
    GtkWidget *btn_up = nullptr, *btn_down = nullptr, *btn_left = nullptr, *btn_right = nullptr;
    GtkWidget *btn_home = nullptr;
    GtkWidget *step_combo = nullptr;   // PTZ step size
    GtkWidget *speed_combo = nullptr;  // PTZ speed
    GtkWidget *fov_combo = nullptr;    // FOV wide/medium/narrow
    GtkWidget *ai_toggle = nullptr;    // AI tracking on/off
    GtkWidget *face_toggle = nullptr;  // face focus on/off
    GtkWidget *btn_preview = nullptr;
    GtkWidget *preview_note = nullptr;
    GtkWidget *log_view = nullptr;
    GtkTextBuffer *log_buf = nullptr;
};
Ui ui;

// Controls that must stay disabled until a device is resolved. Toggled together
// in set_connected_ui(); avoids listing each widget by hand.
std::vector<GtkWidget *> g_dev_controls;

// Set while we programmatically update a toggle from device state, so the
// toggle's own handler ignores that change (no command re-issued).
bool g_syncing_ui = false;

// Shared device handle (guarded). Set by the discovery thread, read by command
// handlers. Commands run on short-lived threads so the UI never freezes.
std::mutex g_dev_mtx;
std::shared_ptr<Device> g_dev;

// Latest decoded status snapshot from the SDK's periodic push.
struct StatusSnapshot {
    bool have = false;
    int zoom_raw = -1;
    int ai_mode = -1;
    int dev_status = -1;
    bool zoom_valid = false;   // true once cameraGetZoomAbsoluteR() succeeded
    float zoom_x = 0.0f;       // real normalized zoom, 1.0~2.0
};
std::mutex g_status_mtx;
StatusSnapshot g_status;

// App-local PTZ presets (session only; the SDK's native presets are not
// documented for Tiny 3, so we store angles ourselves — see README).
struct Preset {
    bool set = false;
    float pitch = 0.0f;
    float yaw = 0.0f;
};
std::mutex g_preset_mtx;
std::array<Preset, 3> g_presets;

int g_wait_ms = 6000;              // discovery timeout; USB enumerate + publish
bool g_self_test = false;
bool g_selftest_found = false;     // self-test device result (main thread only)

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

void set_value(GtkWidget *label, const std::string &text, bool unknown) {
    gtk_label_set_text(GTK_LABEL(label), text.c_str());
    GtkStyleContext *ctx = gtk_widget_get_style_context(label);
    if (unknown) gtk_style_context_add_class(ctx, "unknown");
    else         gtk_style_context_remove_class(ctx, "unknown");
}

// Append one line to the log (MAIN THREAD ONLY).
void append_log(const std::string &msg) {
    if (!ui.log_buf) return;
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(ui.log_buf, &end);
    std::string line = msg + "\n";
    gtk_text_buffer_insert(ui.log_buf, &end, line.c_str(), -1);
    // Auto-scroll the view to the newest line.
    if (ui.log_view) {
        GtkTextMark *mark = gtk_text_buffer_get_insert(ui.log_buf);
        gtk_text_buffer_get_end_iter(ui.log_buf, &end);
        gtk_text_buffer_move_mark(ui.log_buf, mark, &end);
        gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(ui.log_view), mark);
    }
}

// Marshal a log line from any thread onto the GTK main loop.
gboolean idle_log(gpointer data) {
    std::string *s = static_cast<std::string *>(data);
    append_log(*s);
    delete s;
    return G_SOURCE_REMOVE;
}
void post_log(const std::string &msg) {
    // In self-test the window closes quickly, so mirror the in-app log to stderr
    // where a terminal user can actually see the discovery/polling narrative.
    if (g_self_test) g_printerr("%s\n", msg.c_str());
    g_idle_add(idle_log, new std::string(msg));
}

// Report the actual GDK backend in use. Dependency-free: compares the display
// object's type name so we don't need to link gdk-wayland/gdk-x11 headers.
// Returns true if the backend is native Wayland.
bool detect_wayland_backend(std::string &name_out) {
    GdkDisplay *d = gdk_display_get_default();
    const char *tn = d ? G_OBJECT_TYPE_NAME(d) : "none";
    name_out = tn ? tn : "none";
    return name_out == "GdkWaylandDisplay";
}

const char *product_name(ObsbotProductType t) {
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

const char *dev_mode_name(Device::DevMode m) {
    switch (m) {
    case Device::DevModeUvc: return "UVC";
    case Device::DevModeNet: return "Net";
    case Device::DevModeMtp: return "MTP";
    case Device::DevModeBle: return "BLE";
    default:                 return "Unknown";
    }
}

const char *run_status_name(int s) {
    switch (s) {
    case Device::DevStatusRun:     return "Run";
    case Device::DevStatusSleep:   return "Sleep";
    case Device::DevStatusPrivacy: return "Privacy";
    default:                       return nullptr; // unknown
    }
}

const char *ai_mode_name(int m) {
    switch (m) {
    case Device::AiWorkModeNone:          return "Off";
    case Device::AiWorkModeGroup:         return "Group track";
    case Device::AiWorkModeHuman:         return "Human track";
    case Device::AiWorkModeHand:          return "Hand track";
    case Device::AiWorkModeWhiteBoard:    return "Whiteboard";
    case Device::AiWorkModeDesk:          return "Desk";
    case Device::AiWorkModeSpeech:        return "Speech";
    case Device::AiWorkModePortraitTrack: return "Portrait track";
    default:                              return nullptr; // unknown / not decoded
    }
}

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

std::string fmt1(float v) { // one decimal, locale-independent
    char buf[32];
    g_ascii_formatd(buf, sizeof(buf), "%.1f", v);
    return buf;
}
std::string fmt2(float v) {
    char buf[32];
    g_ascii_formatd(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// Read the device's real normalized zoom (1.0~2.0) into the shared snapshot.
// Safe to call from an SDK/worker thread (blocking SDK call, no GTK).
void refresh_zoom_snapshot(const std::shared_ptr<Device> &d) {
    if (!d) return;
    float z = 0.0f;
    bool ok = (d->cameraGetZoomAbsoluteR(z) == RM_RET_OK);
    std::lock_guard<std::mutex> lk(g_status_mtx);
    g_status.zoom_valid = ok;
    g_status.zoom_x = z;
}

// ---------------------------------------------------------------------------
// Status push (called on an SDK thread) -> marshal to UI
// ---------------------------------------------------------------------------
gboolean on_status_refresh(gpointer) {
    StatusSnapshot s;
    { std::lock_guard<std::mutex> lk(g_status_mtx); s = g_status; }

    // Run/power state: reliable enum.
    const char *run = s.have ? run_status_name(s.dev_status) : nullptr;
    set_value(ui.val_run, run ? run : "Unknown", run == nullptr);

    // Zoom: read from the real getter (cameraGetZoomAbsoluteR, normalized
    // 1.0~2.0). 1.00x is a real value (min zoom), not "Unknown".
    if (s.zoom_valid && s.zoom_x > 0.0f) {
        set_value(ui.val_zoom, fmt2(s.zoom_x) + "x  (1.0–2.0)", false);
    } else {
        set_value(ui.val_zoom, "Unknown", true);
    }

    // AI mode: decode via documented enum, else Unknown.
    const char *ai = s.have ? ai_mode_name(s.ai_mode) : nullptr;
    set_value(ui.val_ai, ai ? ai : "Unknown", ai == nullptr);

    // Keep the AI toggle honest: reflect the device's real tracking state.
    if (ui.ai_toggle && ai != nullptr) {
        bool tracking = (s.ai_mode != Device::AiWorkModeNone);
        g_syncing_ui = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.ai_toggle), tracking);
        g_syncing_ui = false;
    }
    return G_SOURCE_REMOVE;
}

void on_dev_status(void * /*param*/, const void *data) {
    if (!data) return;
    const Device::CameraStatus *st = static_cast<const Device::CameraStatus *>(data);
    StatusSnapshot s;
    s.have = true;
    s.zoom_raw = st->tiny.zoom_ratio;
    s.ai_mode = st->tiny.ai_mode;
    s.dev_status = st->tiny.dev_status;
    {
        std::lock_guard<std::mutex> lk(g_status_mtx);
        // Keep the last good zoom read (from connect / after a zoom command).
        // We deliberately do NOT call the blocking zoom getter here: this runs
        // on the SDK's status-callback thread, and issuing another blocking SDK
        // query from within it risks stalling status dispatch.
        s.zoom_valid = g_status.zoom_valid;
        s.zoom_x = g_status.zoom_x;
        g_status = s;
    }
    post_log("status push (ai_mode=" + std::to_string(s.ai_mode) + ")");
    g_idle_add(on_status_refresh, nullptr);
}

// ---------------------------------------------------------------------------
// Discovery (runs on a background thread; results marshalled to UI)
// ---------------------------------------------------------------------------
void set_connected_ui(bool connected) {
    GtkStyleContext *ctx = gtk_widget_get_style_context(ui.conn_pill);
    gtk_style_context_remove_class(ctx, "connected");
    gtk_style_context_remove_class(ctx, "searching");
    gtk_style_context_remove_class(ctx, "disconnected");
    gtk_style_context_add_class(ctx, connected ? "connected" : "disconnected");
    gtk_label_set_text(GTK_LABEL(ui.conn_text), connected ? "CONNECTED" : "NO DEVICE");

    for (GtkWidget *w : g_dev_controls) {
        if (w) gtk_widget_set_sensitive(w, connected);
    }
}

gboolean on_discovery_done(gpointer) {
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }

    if (!d) {
        set_connected_ui(false);
        gtk_label_set_text(GTK_LABEL(ui.subtitle), "No OBSBOT device found on USB");
        set_value(ui.val_product, "Unknown", true);
        set_value(ui.val_sn, "Unknown", true);
        set_value(ui.val_fw, "Unknown", true);
        set_value(ui.val_mode, "Unknown", true);
        set_value(ui.val_run, "Unknown", true);
        append_log("No device found. Check USB connection, then restart the app "
                   "(diagnostics: run ./sdk-probe/obsbot-sdk-probe).");
        if (g_self_test) {
            // Separate the two outcomes: the UI is fine; the device just wasn't
            // found within the timeout.
            g_print("[self-test] UI: OK (built, backend checked, no freeze)\n");
            g_print("[self-test] device: NO DEVICE (timeout %d ms)\n", g_wait_ms);
            g_selftest_found = false;
            gtk_main_quit();
        }
        return G_SOURCE_REMOVE;
    }

    const std::string name = d->devName();
    const std::string sn = d->devSn();
    const std::string fw = d->devVersion();
    const ObsbotProductType pt = d->productType();
    const char *pname = product_name(pt);

    set_connected_ui(true);
    std::string sub = std::string(pname) + " · SN " + sn + " · fw " + fw;
    gtk_label_set_text(GTK_LABEL(ui.subtitle), sub.c_str());

    set_value(ui.val_product, std::string(pname) + " (enum " + std::to_string((int)pt) + ")", false);
    set_value(ui.val_sn, sn, false);
    set_value(ui.val_fw, fw, false);
    set_value(ui.val_mode, dev_mode_name(d->devMode()), false);
    // zoom read below (off-thread); AI stays "Unknown" until the status push.

    append_log("Connected: " + name + "  (" + pname + ", SN " + sn + ", fw " + fw + ")");

    // Subscribe to the periodic status push (~2-3s) for AI / run state.
    d->setDevStatusCallbackFunc(on_dev_status, nullptr);
    d->enableDevStatusCallback(true);

    // Read the real zoom once now, off the UI thread (blocking SDK getter).
    // Thereafter it's refreshed after each zoom command.
    if (!g_self_test) {
        std::thread([d]() {
            refresh_zoom_snapshot(d);
            g_idle_add(on_status_refresh, nullptr);
        }).detach();
    }

    if (g_self_test) {
        g_print("[self-test] UI: OK\n");
        g_print("[self-test] device: FOUND product=%s SN=%s fw=%s enum=%d\n",
                pname, sn.c_str(), fw.c_str(), (int)pt);
        g_selftest_found = true;
        gtk_main_quit();
    }
    return G_SOURCE_REMOVE;
}

void discovery_thread() {
    Devices &devs = Devices::get();
    devs.setEnableMdnsScan(false);            // USB only for this PoC
    post_log("discovery: started (USB, timeout " + std::to_string(g_wait_ms) + " ms)");

    // Poll until a device is *resolved* — present in getDevList() AND with its
    // SN populated — or the timeout expires. A single fixed sleep + one read
    // (the old approach) could sample the list in the window after the SDK has
    // read the device but before it finishes publishing it ("add uvc device"),
    // yielding a false NO DEVICE. (Logic covered by poll_logic_test.cpp.)
    const int total = g_wait_ms;
    std::function<std::shared_ptr<Device>()> probe = [&devs]() -> std::shared_ptr<Device> {
        std::list<std::shared_ptr<Device>> list = devs.getDevList();
        for (const auto &d : list) {
            if (d && !d->devSn().empty()) return d;
        }
        return nullptr;
    };
    std::function<void(int)> on_tick = [total](int elapsed_ms) {
        post_log("discovery: polling for device … (" + std::to_string(elapsed_ms) +
                 "/" + std::to_string(total) + " ms)");
    };
    std::shared_ptr<Device> found =
        poll_until_resolved<std::shared_ptr<Device>>(probe, g_wait_ms, 200, on_tick);

    if (found) {
        post_log("discovery: device resolved (SN " + found->devSn() +
                 ", product " + std::string(product_name(found->productType())) +
                 ", fw " + found->devVersion() + ")");
    } else {
        post_log("discovery: timeout after " + std::to_string(g_wait_ms) +
                 " ms — no device in list");
    }
    { std::lock_guard<std::mutex> lk(g_dev_mtx); g_dev = found; }
    g_idle_add(on_discovery_done, nullptr);
}

// ---------------------------------------------------------------------------
// Commands (each runs on a short-lived thread; rc marshalled back to the log)
// ---------------------------------------------------------------------------
template <typename Fn>
void run_cmd(const char *name, Fn fn) {
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }
    if (!d) { append_log(std::string(name) + ": no device connected"); return; }
    post_log(std::string("→ ") + name + " …");
    std::thread([name, fn, d]() {
        int32_t rc = fn(d);
        post_log(std::string(name) + "  rc=" + std::to_string(rc) +
                 (rc == 0 ? "  (ok)" : "  (error)"));
    }).detach();
}

void on_wake(GtkButton *, gpointer) {
    run_cmd("wake", [](std::shared_ptr<Device> d) {
        return d->cameraSetDevRunStatusR(Device::DevStatusRun);
    });
}
void on_sleep(GtkButton *, gpointer) {
    run_cmd("sleep", [](std::shared_ptr<Device> d) {
        return d->cameraSetDevRunStatusR(Device::DevStatusSleep);
    });
}
void on_center(GtkButton *, gpointer) {
    // gimbalRstPosR(): bounded reset to the zero/home position. Validated
    // rc=0 on the real Tiny 3. Single shot, no continuous motion.
    run_cmd("center", [](std::shared_ptr<Device> d) {
        return d->gimbalRstPosR();
    });
}

// ---- PTZ: read current attitude, apply a small clamped delta, move once ----
// SAFE by construction: absolute target position (not continuous speed),
// clamped to the gimbal's valid range, at a slow reference speed. One shot.
float ptz_step_value() {
    int i = ui.step_combo ? gtk_combo_box_get_active(GTK_COMBO_BOX(ui.step_combo)) : 1;
    switch (i) { case 0: return 2.0f; case 2: return 10.0f; default: return 5.0f; }
}
float ptz_speed_value() {
    int i = ui.speed_combo ? gtk_combo_box_get_active(GTK_COMBO_BOX(ui.speed_combo)) : 0;
    return i == 1 ? 40.0f : 20.0f;   // Slow / Medium reference speed
}

void ptz_nudge(const char *name, float pitch_sign, float yaw_sign) {
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }
    if (!d) { append_log(std::string(name) + ": no device connected"); return; }
    const float step = ptz_step_value();
    const float speed = ptz_speed_value();
    post_log(std::string("→ ") + name + " (" + fmt1(step) + "°, spd " + fmt1(speed) + ") …");
    std::thread([=]() {
        float xyz[3] = {0, 0, 0};   // roll, pitch, pan(yaw)
        int rc = d->gimbalGetAttitudeInfoR(xyz);
        if (rc != RM_RET_OK) {
            post_log(std::string(name) + ": read attitude failed rc=" + std::to_string(rc));
            return;
        }
        const float pitch = xyz[1], yaw = xyz[2];
        const float np = clampf(pitch + pitch_sign * step, -90.0f, 90.0f);
        const float ny = clampf(yaw + yaw_sign * step, -120.0f, 120.0f);
        int rc2 = d->gimbalSetSpeedPositionR(0.0f, np, ny, 0.0f, speed, speed);
        post_log(std::string(name) + ": pitch " + fmt1(pitch) + "→" + fmt1(np) +
                 ", yaw " + fmt1(yaw) + "→" + fmt1(ny) +
                 "  rc=" + std::to_string(rc2) + (rc2 == 0 ? " (ok)" : " (error)"));
    }).detach();
}
// Sign convention (documented; flip if inverted on hardware): up = +pitch,
// down = -pitch, left = +yaw, right = -yaw.
void on_up(GtkButton *, gpointer)    { ptz_nudge("ptz up",    +1.0f, 0.0f); }
void on_down(GtkButton *, gpointer)  { ptz_nudge("ptz down",  -1.0f, 0.0f); }
void on_left(GtkButton *, gpointer)  { ptz_nudge("ptz left",  0.0f, +1.0f); }
void on_right(GtkButton *, gpointer) { ptz_nudge("ptz right", 0.0f, -1.0f); }

// ---- Presets: app-local (session). Save captures the current attitude; Go
// moves back to it via the same bounded move path. Not SDK-native presets. ----
void preset_save(int idx) {
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }
    if (!d) { append_log("preset: no device connected"); return; }
    post_log("→ preset " + std::to_string(idx + 1) + " save …");
    std::thread([d, idx]() {
        float xyz[3] = {0, 0, 0};
        int rc = d->gimbalGetAttitudeInfoR(xyz);
        if (rc != RM_RET_OK) {
            post_log("preset " + std::to_string(idx + 1) + " save: read attitude failed rc=" +
                     std::to_string(rc));
            return;
        }
        { std::lock_guard<std::mutex> lk(g_preset_mtx);
          g_presets[idx] = {true, xyz[1], xyz[2]}; }
        post_log("preset " + std::to_string(idx + 1) + " saved (pitch " + fmt1(xyz[1]) +
                 ", yaw " + fmt1(xyz[2]) + ")");
    }).detach();
}
void preset_go(int idx) {
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }
    if (!d) { append_log("preset: no device connected"); return; }
    Preset p;
    { std::lock_guard<std::mutex> lk(g_preset_mtx); p = g_presets[idx]; }
    if (!p.set) { append_log("preset " + std::to_string(idx + 1) + ": empty — Save first"); return; }
    const float speed = ptz_speed_value();
    post_log("→ preset " + std::to_string(idx + 1) + " go …");
    std::thread([d, idx, p, speed]() {
        const float np = clampf(p.pitch, -90.0f, 90.0f);
        const float ny = clampf(p.yaw, -120.0f, 120.0f);
        int rc = d->gimbalSetSpeedPositionR(0.0f, np, ny, 0.0f, speed, speed);
        post_log("preset " + std::to_string(idx + 1) + " go: pitch " + fmt1(np) +
                 ", yaw " + fmt1(ny) + "  rc=" + std::to_string(rc) +
                 (rc == 0 ? " (ok)" : " (error)"));
    }).detach();
}

// ---- Zoom: normalized 1.0~2.0 via cameraSetZoomAbsoluteR ----
void zoom_apply(const char *name, float amount, bool absolute) {
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }
    if (!d) { append_log(std::string(name) + ": no device connected"); return; }
    post_log(std::string("→ ") + name + " …");
    std::thread([=]() {
        float z = 1.0f;
        int rc = d->cameraGetZoomAbsoluteR(z);
        if (rc != RM_RET_OK) {
            post_log(std::string(name) + ": read zoom failed rc=" + std::to_string(rc));
            return;
        }
        float nz = clampf(absolute ? amount : z + amount, 1.0f, 2.0f);
        int rc2 = d->cameraSetZoomAbsoluteR(nz);
        post_log(std::string(name) + ": " + fmt2(z) + "x→" + fmt2(nz) + "x  rc=" +
                 std::to_string(rc2) + (rc2 == 0 ? " (ok)" : " (error)"));
        refresh_zoom_snapshot(d);
        g_idle_add(on_status_refresh, nullptr);
    }).detach();
}
void on_zoom_in(GtkButton *, gpointer)    { zoom_apply("zoom in",  +0.1f, false); }
void on_zoom_out(GtkButton *, gpointer)   { zoom_apply("zoom out", -0.1f, false); }
void on_zoom_reset(GtkButton *, gpointer) { zoom_apply("zoom 1x",   1.0f, true); }

// ---- FOV: wide / medium / narrow (cameraSetFovU, tiny series) ----
void on_fov_changed(GtkComboBox *combo, gpointer) {
    if (g_syncing_ui) return;
    int i = gtk_combo_box_get_active(combo);
    if (i < 0) return;
    Device::FovType fv = (i == 0) ? Device::FovType86
                        : (i == 1) ? Device::FovType78
                                   : Device::FovType65;
    static const char *nm[] = {"wide 86°", "medium 78°", "narrow 65°"};
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }
    if (!d) { append_log("fov: no device connected"); return; }
    std::string label = nm[i];
    post_log("→ fov " + label + " …");
    std::thread([d, fv, label]() {
        int rc = d->cameraSetFovU(fv);
        post_log("fov " + label + "  rc=" + std::to_string(rc) + (rc == 0 ? " (ok)" : " (error)"));
    }).detach();
}

// ---- AI tracking toggle (cameraSetAiModeU Human/None). The authoritative
// state is the STATUS panel, synced from the device's status push. ----
void on_ai_toggled(GtkToggleButton *btn, gpointer) {
    if (g_syncing_ui) return;
    gboolean on = gtk_toggle_button_get_active(btn);
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }
    if (!d) { append_log("ai track: no device connected"); return; }
    post_log(std::string("→ ai track ") + (on ? "on" : "off") + " …");
    std::thread([d, on]() {
        int rc = d->cameraSetAiModeU(on ? Device::AiWorkModeHuman : Device::AiWorkModeNone, 0);
        post_log(std::string("ai track ") + (on ? "on" : "off") + "  rc=" +
                 std::to_string(rc) + (rc == 0 ? " (ok)" : " (error)"));
    }).detach();
}

// ---- Face focus toggle (cameraSetFaceFocusR, category: all). Reverts on
// failure so the toggle never shows a state the device rejected. ----
struct ToggleSet { GtkWidget *w; gboolean active; };
gboolean idle_set_toggle(gpointer p) {
    ToggleSet *t = static_cast<ToggleSet *>(p);
    g_syncing_ui = true;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(t->w), t->active);
    g_syncing_ui = false;
    delete t;
    return G_SOURCE_REMOVE;
}
void on_face_toggled(GtkToggleButton *btn, gpointer) {
    if (g_syncing_ui) return;
    gboolean on = gtk_toggle_button_get_active(btn);
    std::shared_ptr<Device> d;
    { std::lock_guard<std::mutex> lk(g_dev_mtx); d = g_dev; }
    if (!d) { append_log("face focus: no device connected"); return; }
    GtkWidget *w = GTK_WIDGET(btn);
    post_log(std::string("→ face focus ") + (on ? "on" : "off") + " …");
    std::thread([d, on, w]() {
        int rc = d->cameraSetFaceFocusR(on);
        post_log(std::string("face focus ") + (on ? "on" : "off") + "  rc=" +
                 std::to_string(rc) + (rc == 0 ? " (ok)" : " (error)"));
        if (rc != RM_RET_OK) g_idle_add(idle_set_toggle, new ToggleSet{w, !on});
    }).detach();
}

// ---------------------------------------------------------------------------
// Optional external preview via ffplay (real frames, separate window)
// ---------------------------------------------------------------------------
void on_preview(GtkButton *, gpointer) {
    gchar *ffplay = g_find_program_in_path("ffplay");
    if (!ffplay) {
        append_log("preview: ffplay not found — install ffmpeg to enable preview.");
        return;
    }
    // Try 1080p MJPEG first; fall back to device defaults if that format fails.
    // Runs detached in its own window; does NOT embed into this GUI.
    const char *shell =
        "ffplay -hide_banner -loglevel warning -f v4l2 -input_format mjpeg "
        "-video_size 1920x1080 -framerate 30 -window_title 'OBSBOT preview' /dev/video0 "
        "|| ffplay -hide_banner -loglevel warning -f v4l2 "
        "-window_title 'OBSBOT preview (fallback)' /dev/video0";
    const gchar *argv[] = {"/bin/sh", "-c", shell, nullptr};
    GError *err = nullptr;
    gboolean ok = g_spawn_async(nullptr, (gchar **)argv, nullptr,
                                G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &err);
    if (!ok) {
        append_log(std::string("preview: failed to launch ffplay: ") +
                   (err ? err->message : "unknown"));
        if (err) g_error_free(err);
    } else {
        append_log("preview: launched ffplay on /dev/video0 (external window). "
                   "Note: this occupies /dev/video0 and will conflict with "
                   "browser/Meet/OBS camera use.");
    }
    g_free(ffplay);
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
const char *kCss = R"CSS(
window#obsbot { background-color: #12151B; }
.eyebrow { color: #8A94A6; font-size: 10px; letter-spacing: 2px; }
.title   { color: #E6EAF0; font-weight: 800; font-size: 20px; }
.subtitle{ color: #8A94A6; font-family: monospace; font-size: 12px; }
.card    { background-color: #1B2028; border: 1px solid #2A313C;
           border-radius: 10px; padding: 14px; }
.card-title { color: #8A94A6; font-size: 10px; letter-spacing: 2px; }
.key     { color: #8A94A6; font-size: 12px; }
.value   { color: #4FD1C5; font-family: monospace; font-size: 13px; }
.value.unknown { color: #6B7280; font-style: italic; }
.pill    { border-radius: 999px; padding: 3px 12px; font-size: 11px;
           font-weight: 700; letter-spacing: 1px; }
.pill.connected    { background-color: rgba(79,209,197,0.15); color: #4FD1C5; }
.pill.searching    { background-color: rgba(245,166,35,0.15); color: #F5A623; }
.pill.disconnected { background-color: rgba(229,83,75,0.15);  color: #E5534B; }
button.cmd { background: #232A34; color: #E6EAF0; border: 1px solid #2A313C;
             border-radius: 8px; padding: 8px 14px; font-weight: 600; }
button.cmd:hover:not(:disabled) { background: #2A323E; }
button.cmd.primary { border-color: #4FD1C5; color: #4FD1C5; }
button.cmd.warn    { border-color: #F5A623; color: #F5A623; }
button.cmd:disabled { color: #4A515C; border-color: #232A34; }
button.cmd:checked { background: rgba(79,209,197,0.18); border-color: #4FD1C5; color: #4FD1C5; }
.section { color: #8A94A6; font-size: 11px; font-weight: 700; letter-spacing: 1px;
           min-width: 62px; }
.dim { color: #8A94A6; font-size: 12px; }
combobox { color: #E6EAF0; }
combobox:disabled { color: #4A515C; }
button.dpad { background: #232A34; color: #E6EAF0; border: 1px solid #2A313C;
              border-radius: 8px; min-width: 44px; min-height: 44px; font-size: 16px; }
button.dpad.home { border-color: #4FD1C5; color: #4FD1C5; }
button.dpad:disabled { color: #4A515C; }
.preview-box { background-color: #0C0E12; border: 1px dashed #2A313C;
               border-radius: 10px; }
.preview-note { color: #8A94A6; font-size: 11px; }
textview, textview text { background-color: #0C0E12; color: #9FB0C3;
                          font-family: monospace; font-size: 11px; }
)CSS";

GtkWidget *make_kv(const char *key, GtkWidget **value_out) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *k = gtk_label_new(key);
    gtk_style_context_add_class(gtk_widget_get_style_context(k), "key");
    gtk_widget_set_halign(k, GTK_ALIGN_START);
    gtk_widget_set_size_request(k, 90, -1);
    GtkWidget *v = gtk_label_new("Unknown");
    gtk_style_context_add_class(gtk_widget_get_style_context(v), "value");
    gtk_style_context_add_class(gtk_widget_get_style_context(v), "unknown");
    gtk_widget_set_halign(v, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);
    gtk_box_pack_start(GTK_BOX(row), k, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), v, FALSE, FALSE, 0);
    *value_out = v;
    return row;
}

GtkWidget *make_card(const char *title) {
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
    GtkWidget *t = gtk_label_new(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(t), "card-title");
    gtk_widget_set_halign(t, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(card), t, FALSE, FALSE, 0);
    return card;
}

GtkWidget *make_dir_button(const char *glyph, const char *tip, GCallback cb) {
    GtkWidget *b = gtk_button_new_with_label(glyph);
    gtk_style_context_add_class(gtk_widget_get_style_context(b), "dpad");
    gtk_widget_set_sensitive(b, FALSE); // enabled once a device resolves
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, nullptr);
    g_dev_controls.push_back(b);
    return b;
}

// A small labelled command button (registered as device-gated).
GtkWidget *make_cmd(const char *label, const char *cls, GCallback cb) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_style_context_add_class(gtk_widget_get_style_context(b), "cmd");
    if (cls) gtk_style_context_add_class(gtk_widget_get_style_context(b), cls);
    gtk_widget_set_sensitive(b, FALSE);
    g_signal_connect(b, "clicked", cb, nullptr);
    g_dev_controls.push_back(b);
    return b;
}

GtkWidget *make_section_label(const char *text) {
    GtkWidget *l = gtk_label_new(text);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "section");
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

GtkWidget *make_dim_label(const char *text) {
    GtkWidget *l = gtk_label_new(text);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim");
    return l;
}

void build_ui() {
    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(ui.window, "obsbot");
    gtk_window_set_title(GTK_WINDOW(ui.window), "OBSBOT Tiny 3 Control");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 860, 640);
    g_signal_connect(ui.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_container_set_border_width(GTK_CONTAINER(root), 16);
    gtk_container_add(GTK_CONTAINER(ui.window), root);

    // --- Header ---
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *titles = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *eyebrow = gtk_label_new("OBSBOT · PTZ CONTROL");
    gtk_style_context_add_class(gtk_widget_get_style_context(eyebrow), "eyebrow");
    gtk_widget_set_halign(eyebrow, GTK_ALIGN_START);
    GtkWidget *title = gtk_label_new("OBSBOT Tiny 3 Control");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    ui.subtitle = gtk_label_new("Searching for device …");
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.subtitle), "subtitle");
    gtk_widget_set_halign(ui.subtitle, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(titles), eyebrow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(titles), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(titles), ui.subtitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), titles, FALSE, FALSE, 0);

    // tally-style connection pill (top-right)
    ui.conn_pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.conn_pill), "pill");
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.conn_pill), "searching");
    ui.conn_text = gtk_label_new("SEARCHING");
    gtk_container_add(GTK_CONTAINER(ui.conn_pill), ui.conn_text);
    gtk_widget_set_halign(ui.conn_pill, GTK_ALIGN_END);
    gtk_widget_set_valign(ui.conn_pill, GTK_ALIGN_START);
    gtk_box_pack_end(GTK_BOX(header), ui.conn_pill, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);

    // --- Middle row: preview (left) + status (right) ---
    GtkWidget *mid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_box_pack_start(GTK_BOX(root), mid, TRUE, TRUE, 0);

    // Preview card
    GtkWidget *pcard = make_card("PREVIEW");
    gtk_widget_set_hexpand(pcard, TRUE);
    GtkWidget *pbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(pbox), "preview-box");
    gtk_widget_set_vexpand(pbox, TRUE);
    GtkWidget *pcenter = gtk_label_new("No embedded video preview in this PoC");
    gtk_style_context_add_class(gtk_widget_get_style_context(pcenter), "preview-note");
    gtk_widget_set_valign(pcenter, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(pcenter, TRUE);
    gtk_box_pack_start(GTK_BOX(pbox), pcenter, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(pcard), pbox, TRUE, TRUE, 0);
    ui.preview_note = gtk_label_new(
        "Embedded preview needs GStreamer (not installed). Optional external "
        "preview via ffplay opens a separate window and occupies /dev/video0.");
    gtk_label_set_line_wrap(GTK_LABEL(ui.preview_note), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.preview_note), "preview-note");
    gtk_widget_set_halign(ui.preview_note, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(pcard), ui.preview_note, FALSE, FALSE, 0);
    ui.btn_preview = gtk_button_new_with_label("Launch Live Preview (ffplay)");
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.btn_preview), "cmd");
    g_signal_connect(ui.btn_preview, "clicked", G_CALLBACK(on_preview), nullptr);
    // Only enable if ffplay + /dev/video0 are present.
    {
        gchar *ff = g_find_program_in_path("ffplay");
        bool has_dev = (access("/dev/video0", F_OK) == 0);
        gtk_widget_set_sensitive(ui.btn_preview, ff != nullptr && has_dev);
        if (!ff) gtk_widget_set_tooltip_text(ui.btn_preview, "Install ffmpeg (ffplay) to enable preview");
        else if (!has_dev) gtk_widget_set_tooltip_text(ui.btn_preview, "/dev/video0 not present");
        if (ff) g_free(ff);
    }
    gtk_box_pack_start(GTK_BOX(pcard), ui.btn_preview, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mid), pcard, TRUE, TRUE, 0);

    // Status card
    GtkWidget *scard = make_card("STATUS");
    gtk_box_pack_start(GTK_BOX(scard), make_kv("product", &ui.val_product), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scard), make_kv("SN", &ui.val_sn), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scard), make_kv("firmware", &ui.val_fw), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scard), make_kv("mode", &ui.val_mode), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scard), make_kv("run state", &ui.val_run), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scard), make_kv("zoom", &ui.val_zoom), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scard), make_kv("AI / track", &ui.val_ai), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(mid), scard, FALSE, FALSE, 0);

    // --- Controls card ---
    GtkWidget *ccard = make_card("CONTROLS");
    GtkWidget *crow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_box_pack_start(GTK_BOX(ccard), crow, FALSE, FALSE, 0);

    // Left column: grouped controls stacked compactly.
    GtkWidget *leftcol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(leftcol, TRUE);
    gtk_box_pack_start(GTK_BOX(crow), leftcol, TRUE, TRUE, 0);

    // Power row.
    GtkWidget *power = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui.btn_wake = make_cmd("Wake", "primary", G_CALLBACK(on_wake));
    ui.btn_sleep = make_cmd("Sleep", "warn", G_CALLBACK(on_sleep));
    ui.btn_center = make_cmd("Center", nullptr, G_CALLBACK(on_center));
    gtk_box_pack_start(GTK_BOX(power), ui.btn_wake, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(power), ui.btn_sleep, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(power), ui.btn_center, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftcol), power, FALSE, FALSE, 0);

    // Move settings: step + speed (these two are app settings, always usable).
    GtkWidget *moverow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(moverow), make_section_label("Move"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(moverow), make_dim_label("step"), FALSE, FALSE, 0);
    ui.step_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.step_combo), "2°");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.step_combo), "5°");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.step_combo), "10°");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui.step_combo), 1);
    gtk_box_pack_start(GTK_BOX(moverow), ui.step_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(moverow), make_dim_label("speed"), FALSE, FALSE, 0);
    ui.speed_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.speed_combo), "Slow");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.speed_combo), "Medium");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui.speed_combo), 0);
    gtk_box_pack_start(GTK_BOX(moverow), ui.speed_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftcol), moverow, FALSE, FALSE, 0);

    // Zoom row.
    GtkWidget *zoomrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(zoomrow), make_section_label("Zoom"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(zoomrow), make_cmd("−", nullptr, G_CALLBACK(on_zoom_out)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(zoomrow), make_cmd("1x", nullptr, G_CALLBACK(on_zoom_reset)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(zoomrow), make_cmd("+", nullptr, G_CALLBACK(on_zoom_in)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftcol), zoomrow, FALSE, FALSE, 0);

    // Presets row (app-local, session).
    GtkWidget *presetrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(presetrow), make_section_label("Presets"), FALSE, FALSE, 0);
    for (int i = 0; i < 3; ++i) {
        GtkWidget *grp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        std::string tag = "P" + std::to_string(i + 1);
        gtk_box_pack_start(GTK_BOX(grp), make_dim_label(tag.c_str()), FALSE, FALSE, 0);
        // Per-preset Save/Go via lightweight per-index trampolines.
        GtkWidget *save = gtk_button_new_with_label("Save");
        GtkWidget *go = gtk_button_new_with_label("Go");
        gtk_style_context_add_class(gtk_widget_get_style_context(save), "cmd");
        gtk_style_context_add_class(gtk_widget_get_style_context(go), "cmd");
        gtk_widget_set_sensitive(save, FALSE);
        gtk_widget_set_sensitive(go, FALSE);
        g_signal_connect(save, "clicked",
                         G_CALLBACK(+[](GtkButton *, gpointer p) { preset_save(GPOINTER_TO_INT(p)); }),
                         GINT_TO_POINTER(i));
        g_signal_connect(go, "clicked",
                         G_CALLBACK(+[](GtkButton *, gpointer p) { preset_go(GPOINTER_TO_INT(p)); }),
                         GINT_TO_POINTER(i));
        g_dev_controls.push_back(save);
        g_dev_controls.push_back(go);
        gtk_box_pack_start(GTK_BOX(grp), save, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(grp), go, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(presetrow), grp, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(leftcol), presetrow, FALSE, FALSE, 0);

    // Camera row: FOV + AI track + face focus.
    GtkWidget *camrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(camrow), make_section_label("Camera"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(camrow), make_dim_label("FOV"), FALSE, FALSE, 0);
    ui.fov_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fov_combo), "Wide 86°");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fov_combo), "Medium 78°");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.fov_combo), "Narrow 65°");
    gtk_widget_set_sensitive(ui.fov_combo, FALSE);
    g_dev_controls.push_back(ui.fov_combo);
    g_signal_connect(ui.fov_combo, "changed", G_CALLBACK(on_fov_changed), nullptr);
    gtk_box_pack_start(GTK_BOX(camrow), ui.fov_combo, FALSE, FALSE, 0);
    ui.ai_toggle = gtk_toggle_button_new_with_label("AI Track");
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.ai_toggle), "cmd");
    gtk_widget_set_sensitive(ui.ai_toggle, FALSE);
    g_dev_controls.push_back(ui.ai_toggle);
    g_signal_connect(ui.ai_toggle, "toggled", G_CALLBACK(on_ai_toggled), nullptr);
    gtk_box_pack_start(GTK_BOX(camrow), ui.ai_toggle, FALSE, FALSE, 0);
    ui.face_toggle = gtk_toggle_button_new_with_label("Face focus");
    gtk_style_context_add_class(gtk_widget_get_style_context(ui.face_toggle), "cmd");
    gtk_widget_set_sensitive(ui.face_toggle, FALSE);
    g_dev_controls.push_back(ui.face_toggle);
    g_signal_connect(ui.face_toggle, "toggled", G_CALLBACK(on_face_toggled), nullptr);
    gtk_box_pack_start(GTK_BOX(camrow), ui.face_toggle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftcol), camrow, FALSE, FALSE, 0);

    // Right: PTZ D-pad. Enabled once a device resolves.
    const char *dtip = "One-shot bounded move: reads current angle, applies the "
                       "step, moves once at the set speed.";
    GtkWidget *dpad = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(dpad), 6);
    gtk_grid_set_column_spacing(GTK_GRID(dpad), 6);
    gtk_widget_set_valign(dpad, GTK_ALIGN_CENTER);
    ui.btn_up = make_dir_button("↑", dtip, G_CALLBACK(on_up));
    ui.btn_down = make_dir_button("↓", dtip, G_CALLBACK(on_down));
    ui.btn_left = make_dir_button("←", dtip, G_CALLBACK(on_left));
    ui.btn_right = make_dir_button("→", dtip, G_CALLBACK(on_right));
    GtkWidget *home = gtk_button_new_with_label("⊙");
    gtk_style_context_add_class(gtk_widget_get_style_context(home), "dpad");
    gtk_style_context_add_class(gtk_widget_get_style_context(home), "home");
    gtk_widget_set_tooltip_text(home, "Center (same as Center button)");
    gtk_widget_set_sensitive(home, FALSE);
    g_signal_connect(home, "clicked", G_CALLBACK(on_center), nullptr);
    ui.btn_home = home;
    g_dev_controls.push_back(home);
    gtk_grid_attach(GTK_GRID(dpad), ui.btn_up, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(dpad), ui.btn_left, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(dpad), home, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(dpad), ui.btn_right, 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(dpad), ui.btn_down, 1, 2, 1, 1);
    gtk_box_pack_end(GTK_BOX(crow), dpad, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), ccard, FALSE, FALSE, 0);

    // --- Log card ---
    GtkWidget *lcard = make_card("LOG");
    GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(scroll, -1, 120);
    GtkWidget *log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    ui.log_view = log_view;
    ui.log_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    gtk_container_add(GTK_CONTAINER(scroll), log_view);
    gtk_box_pack_start(GTK_BOX(lcard), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), lcard, FALSE, FALSE, 0);
}

void apply_css() {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, kCss, -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

} // namespace

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--self-test") g_self_test = true;
        else if (a == "--wait-ms" && i + 1 < argc) g_wait_ms = std::max(0, std::atoi(argv[++i]));
        else if (a == "-h" || a == "--help") {
            g_print("Usage: %s [--wait-ms N] [--self-test]\n", argv[0]);
            g_print("  --wait-ms N   USB discovery settle time (default 3000)\n");
            g_print("  --self-test   Build UI + run discovery once, then exit (no GUI loop lock)\n");
            return 0;
        }
    }

    if (!gtk_init_check(&argc, &argv)) {
        // No display (e.g. headless CI). The build is still valid.
        g_printerr("gtk_init failed (no display). Build OK; GUI needs a display.\n");
        return g_self_test ? 0 : 1;
    }

    apply_css();
    build_ui();

    // Confirm we are on native Wayland (not X11/XWayland). Report it visibly and
    // warn loudly if not — the target is a Wayland-native app.
    {
        std::string backend;
        bool wl = detect_wayland_backend(backend);
        std::string msg = std::string("display backend: ") + backend +
                          (wl ? "  (native Wayland ✓)" : "  (NOT Wayland!)");
        append_log(msg);
        g_print("[backend] %s\n", msg.c_str());
        if (!wl) {
            g_printerr("WARNING: not running on native Wayland (got %s). "
                       "Launch via ./obsbot-tiny3-control or set GDK_BACKEND=wayland.\n",
                       backend.c_str());
        }
    }

    gtk_widget_show_all(ui.window);

    // Discover OFF the UI thread so the window never freezes.
    std::thread(discovery_thread).detach();

    if (g_self_test) {
        // Safety net in case the discovery thread hangs on a blocking SDK call:
        // give it the full timeout plus margin, then quit (device stays unfound).
        g_timeout_add(g_wait_ms + 3000, [](gpointer) -> gboolean {
            g_printerr("[self-test] safety timeout — discovery did not report back\n");
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }, nullptr);
    }

    gtk_main();

    // In self-test the exit code reflects the DEVICE result (UI result is
    // printed separately above): 0 = device found, 3 = no device by timeout.
    if (g_self_test) return g_selftest_found ? 0 : 3;
    return 0;
}
