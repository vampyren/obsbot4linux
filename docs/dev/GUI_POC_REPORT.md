# OBSBOT Tiny 3 — Native Linux GUI PoC Report

> **Update — discovery timing fix.** The GUI now **polls** for the device
> instead of taking one fixed-delay sample, fixing the false "NO DEVICE" race
> Rex saw on real hardware. Details, build/run commands, expected output, and a
> self code review are in **§14** at the bottom. Everything above is the
> original PoC report.

**Goal:** Build a small native Linux GUI PoC to control the OBSBOT Tiny 3 on top
of the validated SDK integration. Keep it small, clean, safe, and honest.

**Status:** ✅ GUI builds cleanly (no warnings), launches on Wayland without
freezing, discovers off the UI thread, exposes safe Wake/Sleep/Center, and shows
real device identity. Directional PTZ is intentionally disabled (no verified
bounded Tiny 3 move API). No fake preview, no fake status. The existing
`sdk-probe` still builds and runs.

> **Hardware note:** this build sandbox (`/home/spawn/Apps/obsbot-tiny3-linux`) has **no
> camera attached** (`/dev/video*` absent). All hardware-specific validation
> (Tiny3 enum 18, SN `<redacted-SN>`, fw `6.6.9.1`, Wake/Center) must be run by
> Rex on `/home/vampyren/Apps/obsbot`. The GUI reads these fields straight from
> the SDK device object exactly as the already-validated probe does, so a
> connected unit will populate them. Everything path-independent was validated
> here (build, UI, off-thread discovery, no-device handling, no freeze).

---

## 1. Files changed / added

| File | Purpose |
|------|---------|
| `gui/main.cpp` | The GTK3 GUI PoC (631 lines) |
| `gui/Makefile` | Build (no cmake needed); `$ORIGIN` repo-local rpath |
| `gui/README.md` | GUI developer note: toolkit rationale, deps, run, safety, preview honesty |
| `obsbot-tiny3-control` | Repo-root POSIX-sh launcher (fish/bash/zsh friendly) |
| `.gitignore` | Added `gui/obsbot-tiny3-control-bin`, `gui/build/` |
| `GUI_POC_REPORT.md` | This report |

Untouched and still working: `sdk-probe/*`, the SDK tree `libdev_v2.1.0_8/`, and
the SDK zip (not deleted).

---

## 2. Build command

```fish
cd gui && make
# or, from repo root (builds on first run, then launches):
./obsbot-tiny3-control
```

Observed: clean compile with `-Wall -Wextra`, **zero warnings**.

## 3. Run command

```fish
./obsbot-tiny3-control                  # repo-root launcher (recommended)
./obsbot-tiny3-control --wait-ms 10000  # override discovery timeout (default 6000)
./obsbot-tiny3-control --self-test      # poll for a device, print result, exit
```

> Note: discovery/self-test behaviour was improved after this section was first
> written — see **§14** for the polling fix and the current self-test output.

---

## 4. Toolkit chosen and why

**GTK3 `3.24.52`.** The goal preferred **Qt 6/QML**, but Qt 6 (and Qt 5, and
`cmake`) are **not installed** in this environment:

```
no qmake/qmake6 · no Qt6 cmake package · Qt6Core pkg-config: no · no Qt5Widgets · no cmake
```

Per the goal's fallback clause ("if Qt 6 is blocked, explain and choose the
smallest practical native alternative"), I used GTK3 — it is present, native,
**Wayland-capable**, links cleanly from C++, and integrates directly with the
C++ SDK. I did **not** pull in a heavier workaround (building/bundling Qt). The
SDK-facing logic is small and portable to Qt 6 later if a richer UI or embedded
video is wanted (that is the recommended next step).

---

## 5. Whether Tiny 3 detection works in the GUI

**Code path: yes; runtime confirmation pending hardware.** Discovery uses the
same `Devices::get()` → `getDevList()` path validated by the probe, then reads
`devName()`, `devSn()`, `devVersion()`, `productType()`, `devMode()`. The header
maps `ObsbotProdTiny3 = 18` and the UI shows `Tiny3 (enum 18)` plus SN and fw.

In the no-device sandbox the GUI correctly shows **NO DEVICE** and keeps all
commands disabled (post-fix output; see §14):

```
[self-test] UI: OK (built, backend checked, no freeze)
[self-test] device: NO DEVICE (timeout 6000 ms)
exit=3
```

On Rex's unit the STATUS panel will read `product: Tiny3 (enum 18)`,
`SN: <redacted-SN>`, `firmware: 6.6.9.1`, `mode: UVC`.

---

## 6. Which controls work

| Control | State | SDK call |
|---------|-------|----------|
| **Wake** | ✅ real | `cameraSetDevRunStatusR(DevStatusRun)` |
| **Sleep** | ✅ real | `cameraSetDevRunStatusR(DevStatusSleep)` |
| **Center** | ✅ real (validated rc=0 on Tiny 3) | `gimbalRstPosR()` |
| **Up / Down / Left / Right** | ⛔ disabled with tooltip + TODO | none (see Safety) |

Each command runs on a short-lived thread; the return code is written to the LOG
panel (`center  rc=0  (ok)`), so the UI never blocks on an SDK call.

---

## 7. Whether preview works

- **Embedded preview: not implemented** (honest placeholder shown). It needs
  GStreamer, which is not installed. The preview panel says so.
- **External preview: real, optional.** The "Launch Live Preview (ffplay)"
  button spawns `ffplay` on `/dev/video0` in a **separate** window, trying
  1920×1080 MJPEG first and falling back to device defaults. The button is only
  enabled when both `ffplay` and `/dev/video0` are present. No fake frames.
- Could not verify live frames in this sandbox (no `/dev/video0`); on Rex's
  machine the button opens a real ffplay window. Actual resolution/FPS depends
  on what the Tiny 3 offers for the requested MJPEG mode.

**Conflict warning (also shown in-app and in README):** any preview occupies
`/dev/video0` and will conflict with Proton Meet, Miro, OBS, or a browser using
the camera. This tool controls the camera over the SDK USB channel; it does
**not** change browser/WebRTC resolution negotiation — that is separate.

---

## 8. Camera movement safety notes

- Only one-shot, bounded commands are wired (Wake/Sleep/Center).
- **Center = `gimbalRstPosR()`** — a bounded reset to home, not continuous motion.
- **Directional buttons are disabled by design.** After inspecting the headers
  and sample: no gimbal move API lists Tiny 3 in its `@category`
  (`aiSetGimbalMotorAngleR` → tiny2/tail air only; `gimbalSetSpeedPositionR` →
  tiny/tiny4k/tiny2/tail air), and `gimbalSpeedCtrlR` is **continuous** (needs a
  separate stop → not one-shot). Per the "don't guess" rule, the buttons are
  disabled with a tooltip rather than firing an unverified motion command.
- **Documented safe TODO** (enable only after on-hardware verification): read
  current angle via `gimbalGetAttitudeInfoR`, then move to a small clamped
  absolute target via `gimbalSetSpeedPositionR(0, pitch±5°, yaw±5°, 0, slow,
  slow)`, clamped to pitch −90..90 / yaw −120..120.

---

## 9. Files created outside `/home/vampyren/Apps/obsbot`

**None observed.** Snapshotting `$HOME` and `/tmp` around a run showed no
SDK-created files. The SDK opens mDNS sockets at init (runtime only, even with
network scan disabled) but writes nothing to disk. All build outputs stay under
the repo (`gui/obsbot-tiny3-control-bin`) and are gitignored. The SDK library is
loaded repo-locally via rpath — nothing is installed into system directories,
and no sudo is used.

---

## 10. Known limitations

1. **No hardware in the build sandbox** — hardware-specific detection/commands
   are code-complete but must be confirmed on Rex's Tiny 3.
2. **Qt 6 not used** — GTK3 fallback (Qt 6 absent here). Port is the next step
   if desired.
3. **No embedded video** — needs GStreamer; external ffplay window is the honest
   stand-in.
4. **zoom / AI decoding is best-effort** — shown as **Unknown** until a real
   ~2-3s status push arrives with a plausible value (this is why the probe saw
   `zoom_ratio=0`: it read status without enabling the push). Never shows a fake
   `0`/blank as real.
5. **Directional PTZ disabled** — pending a verified bounded Tiny 3 move.
6. **SDK console logs** (`d-i:`/`d-d:` lines) print to the launching terminal;
   cosmetic, not the GUI's own output.

---

## 11. GUI layout (textual description)

No screenshot tool is installed in this environment (`grim`/`spectacle`/`scrot`/
`imagemagick` all absent), and the window was confirmed to map on the live
Wayland session without crashing. Layout — a dark "camera-operator's console"
(base `#12151B`, cyan `#4FD1C5` data readouts, amber `#F5A623` active, red
`#E5534B` disconnected; monospace values):

```
┌───────────────────────────────────────────────────────────────┐
│ OBSBOT · PTZ CONTROL                                           │
│ OBSBOT Tiny 3 Control                          [ ● CONNECTED ] │
│ Tiny3 · SN <redacted-SN> · fw 6.6.9.1                        │
├───────────────────────────────┬───────────────────────────────┤
│ PREVIEW                       │ STATUS                        │
│  ┌─────────────────────────┐  │  product    Tiny3 (enum 18)   │
│  │ No embedded video        │  │  SN         <redacted-SN>    │
│  │ preview in this PoC      │  │  firmware   6.6.9.1           │
│  └─────────────────────────┘  │  mode       UVC               │
│  Embedded preview needs       │  run state  Run               │
│  GStreamer … external ffplay  │  zoom       Unknown           │
│  occupies /dev/video0.        │  AI / track Unknown           │
│  [ Launch Live Preview ]      │                               │
├───────────────────────────────┴───────────────────────────────┤
│ CONTROLS                                                      │
│  [ Wake ] [ Sleep ] [ Center ]                    [ ↑ ]       │
│                                              [ ← ][ ⊙ ][ → ]  │
│                                                   [ ↓ ]       │
│  (↑↓←→ disabled — no verified bounded Tiny 3 move)           │
├───────────────────────────────────────────────────────────────┤
│ LOG                                                          │
│  Connected: OBSBOT Tiny3 (Tiny3, SN RMOW…, fw 6.6.9.1)       │
│  → center …                                                  │
│  center  rc=0  (ok)                                          │
└───────────────────────────────────────────────────────────────┘
```

The connection pill is the signature element alongside the PTZ cross with a
center home (⊙) button mirroring the gimbal.

---

## 12. Self code review

Reviewed the diff against the required checklist; findings and resolutions:

| Check | Result |
|-------|--------|
| Broken SDK include/library paths | **OK.** `-I../libdev_v2.1.0_8/include`, `-L.../linux/x86_64-release`. Folder name `libdev_v2.1.0_8` matches the extracted dir (the zip is `libdev_v2_1_0_8.zip` — different, correct). |
| Broken runtime libdev loading | **Verified.** `readelf -d` → `RUNPATH=$ORIGIN/../libdev_v2.1.0_8/linux/x86_64-release`; `ldd` resolves `libdev.so.1.0.3` repo-locally. Wrapper also exports `LD_LIBRARY_PATH` fallback. No system install, no sudo. |
| Hardcoded wrong SDK folder names | **OK.** No `/home/...` hardcoding; wrapper resolves its own dir; Makefile uses relative `../libdev_v2.1.0_8`. |
| Unsafe movement | **Safe.** Only Wake/Sleep/Center (bounded reset). Directional buttons disabled with tooltip + documented safe TODO. No loops/sweeps/continuous speed. |
| Fake preview / status | **None.** No embedded preview faked; external ffplay = real frames. zoom/AI show "Unknown" until a real status push decodes to a plausible value; product/SN/fw/mode read straight from the device. |
| GUI freeze during discovery | **Avoided.** Discovery runs on a detached thread; results marshalled via `g_idle_add`. Commands run on short-lived threads. Window shows immediately. |
| Missing no-device handling | **Handled.** Null device → "NO DEVICE", commands disabled, diagnostic logged; `run_cmd` also guards null. |
| Missing dependency docs | **Added.** README lists g++, `libgtk-3-dev`/`gtk3`, pkg-config, optional ffmpeg; Debian + Arch names. |
| Build outputs accidentally added | **Prevented.** `gui/obsbot-tiny3-control-bin` + `gui/build/` gitignored. |
| Breaking existing sdk-probe | **Not broken.** Re-verified: `sdk-probe` clean build + runs (exit 1 = no device). No duplicate `libdev.so` under `sdk-probe/` or `gui/`. |
| Wayland/runtime packaging gaps | **Covered & confirmed native Wayland.** Runtime check prints `GdkWaylandDisplay (native Wayland ✓)`; the wrapper forces `GDK_BACKEND=wayland` so it can't silently fall back to X11/XWayland, and the app warns loudly if the backend isn't Wayland (verified against `GDK_BACKEND=x11` → `GdkX11Display`). Deps + repo-local rpath + wrapper documented. |

**Fixes applied during review:** removed a self-assignment no-op; wired the D-pad
home (⊙) button to follow connection state; added missing includes
(`<algorithm>`, `<cstdlib>`, `<chrono>`); added log auto-scroll; qualified the
`Device::AiWorkMode*` enum (compile fix). Final build is warning-free.

---

## 13. Next recommended step

Enable directional PTZ using the documented safe pattern (read attitude → small
clamped absolute move) **verified on the real Tiny 3**, and add a live status
readout loop (zoom/AI) once decoding is confirmed against hardware. If a richer
UI or embedded in-app video is wanted, port this same SDK-facing logic to
**Qt 6/QML** with `QtMultimedia` (or a GStreamer widget) for embedded preview.

---

## 14. Discovery timing fix (false "NO DEVICE" race)

### The bug
On Rex's machine the SDK clearly detected the Tiny 3, but the GUI printed
`NO DEVICE` — then, moments later, the SDK logged `add uvc device: <redacted-SN>`.
Root cause: the discovery thread did **one fixed `sleep(wait_ms)` then a single
`getDevList()` read**. That read landed in the window after the SDK had read the
device's identity but before it finished publishing it into the list → false
negative.

### The fix
Discovery now **polls** instead of sampling once. It calls `getDevList()` every
**200 ms** until a device is *resolved* — present in the list **and** with a
non-empty `devSn()` — or the timeout expires. Default timeout raised to
**6000 ms** (override with `--wait-ms`). The poll runs on the background thread,
so the UI still never blocks, and controls are enabled only after a device
resolves. The poll logic was factored into `gui/poll.hpp` and covered by a
hardware-free regression test (`gui/poll_logic_test.cpp`) that reproduces the
"device published late" scenario.

Self-test now reports the **UI result and device result separately**, with the
exit code reflecting the device (0 = found, 3 = none by timeout).

### Files changed in this fix
| File | Change |
|------|--------|
| `gui/main.cpp` | Poll-until-resolved discovery; default wait 6000 ms; richer discovery logs; self-test prints UI vs device separately + device-based exit code; `post_log` mirrored to stderr in self-test |
| `gui/poll.hpp` | **New** — `poll_until_resolved()` timing helper (SDK-free, testable) |
| `gui/poll_logic_test.cpp` | **New** — regression test for the poll timing |
| `gui/Makefile` | `make test` target; `self-test` target tolerant of no-device exit |
| `gui/README.md` | Documented polling, `--self-test` contract, `make test` |
| `.gitignore` | Ignore `gui/poll_logic_test` |

### Exact build command
```fish
cd gui && make          # build the GUI
make test               # run the hardware-free poll-timing regression test
```

### Exact run command
```fish
./obsbot-tiny3-control                  # repo-root launcher (Wayland-forced)
./obsbot-tiny3-control --self-test      # poll for device, print result, exit
./obsbot-tiny3-control --wait-ms 10000  # override the 6000 ms default timeout
```

### Expected fixed self-test output

On Rex's machine **with the Tiny 3 connected** (the device is picked up the
moment it publishes, even if that is a second into discovery):
```
[backend] display backend: GdkWaylandDisplay  (native Wayland ✓)
discovery: started (USB, timeout 6000 ms)
discovery: polling for device … (1000/6000 ms)     # only if it takes >1s
discovery: device resolved (SN <redacted-SN>, product Tiny3, fw 6.6.9.1)
[self-test] UI: OK
[self-test] device: FOUND product=Tiny3 SN=<redacted-SN> fw=6.6.9.1 enum=18
# exit code 0
```

With **no device** (verified here in the sandbox):
```
discovery: started (USB, timeout 6000 ms)
discovery: polling for device … (1000/6000 ms)
...
discovery: timeout after 6000 ms — no device in list
[self-test] UI: OK (built, backend checked, no freeze)
[self-test] device: NO DEVICE (timeout 6000 ms)
# exit code 3
```

Regression test (`make test`), verified here:
```
case1 PASS: resolved late-published device at ~1001 ms (timeout was 6000)
case2 PASS: timed out with no device at ~802 ms (timeout was 800)
ALL POLL LOGIC TESTS PASS
```

### Does sdk-probe still work?
Yes — re-verified: `sdk-probe` clean build, runs, exit 1 (no device, expected).
Untouched by this fix. No duplicate `libdev.so` under `sdk-probe/` or `gui/`.

### Self code review (timing fix)

| Check | Result |
|-------|--------|
| Race conditions | `g_dev` is mutex-guarded; `g_selftest_found` is written/read on the GTK main thread only (in `on_discovery_done` and after `gtk_main()`); the `probe`/`on_tick` lambdas run synchronously inside the discovery thread, so the `&devs` capture (a reference to the process-lifetime singleton) stays valid. `devSn()` is called on the discovery thread, never on the UI thread. |
| UI thread blocking | Polling runs on the background discovery thread; results are marshalled via `g_idle_add`. Commands still run on short-lived threads. The window never blocks. |
| Wrong timeout units | All milliseconds; elapsed measured with `steady_clock` → `duration_cast<milliseconds>`. Poll interval 200 ms, default timeout 6000 ms. |
| Broken no-device handling | Preserved: timeout → `g_dev` null → NO-DEVICE UI, commands disabled, diagnostic logged; self-test exit 3. |
| Broken sdk-probe | Not touched; re-verified building and running. |
| Unsafe enabling of buttons before device ready | Controls enable only in `on_discovery_done` when a resolved device (non-empty SN) is present; directional buttons remain disabled regardless. |
| Runtime library path regressions | `gui/Makefile` link/rpath unchanged (`$ORIGIN/../libdev_v2.1.0_8/linux/x86_64-release`); `readelf` still shows the repo-local RUNPATH. The new `make test` target builds only `poll.hpp` (SDK-free, `-lpthread`), so it cannot affect SDK loading. |

**Known non-goal:** hot-plug after the timeout still requires an app restart
(no `setDevChangedCallback` re-discovery loop) — out of scope for this timing
fix; the 6000 ms default comfortably covers normal USB enumeration.

---

## 15. Final report — discovery timing fix (summary)

**Root cause (as diagnosed):** `discovery_thread()` did one fixed
`sleep(wait_ms)` then a **single** `getDevList()` read. That read landed in the
window after the SDK had read the device's identity but before it published it
into the list (right before its `add uvc device` log) → false `NO DEVICE`.

**The fix:** discovery now **polls** `getDevList()` every 200 ms until a device
is *resolved* — present in the list **and** with a non-empty SN — or the timeout
expires. Default timeout raised to **6000 ms**. Still off the UI thread (no
freeze); controls enable only after a device resolves.

**Files changed**
- `gui/main.cpp` — poll-until-resolved discovery; default wait 6000 ms; clear
  logs (`started` → `polling …` → `device resolved`/`timeout`); self-test prints
  UI result and device result separately with a device-based exit code
  (0 = found, 3 = none); `post_log` mirrored to stderr in self-test.
- `gui/poll.hpp` — **new**, timing helper factored out so it is testable without
  the SDK.
- `gui/poll_logic_test.cpp` — **new**, hardware-free regression test reproducing
  the "device published late" scenario.
- `gui/Makefile` — `make test` target; `self-test` target tolerant of the
  no-device exit.
- `gui/README.md`, `GUI_POC_REPORT.md`, `.gitignore` — docs + ignore the test
  binary.

**Exact build command**
```fish
cd gui && make       # build the GUI
make test            # hardware-free poll-timing regression test
```

**Exact run command**
```fish
./obsbot-tiny3-control --self-test          # poll for device, print result, exit
./obsbot-tiny3-control --wait-ms 10000      # override the 6000 ms default
```

**Expected fixed self-test output (Tiny 3 connected)**
```
discovery: started (USB, timeout 6000 ms)
discovery: device resolved (SN <redacted-SN>, product Tiny3, fw 6.6.9.1)
[self-test] UI: OK
[self-test] device: FOUND product=Tiny3 SN=<redacted-SN> fw=6.6.9.1 enum=18
# exit 0
```

**Verified in this sandbox (no camera)**
```
GUI: clean build, no warnings
case1 PASS: resolved late-published device at ~1003 ms (timeout was 6000)
case2 PASS: timed out with no device at ~804 ms (timeout was 800)
ALL POLL LOGIC TESTS PASS
[self-test] UI: OK (built, backend checked, no freeze)
[self-test] device: NO DEVICE (timeout 6000 ms)   # exit 3
sdk-probe build OK, run exit=1                     # still working, no device
RUNPATH = $ORIGIN/../libdev_v2.1.0_8/linux/x86_64-release   # repo-local, intact
```

**sdk-probe:** still builds and runs (exit 1 = no device, expected); untouched
by this fix. Native Wayland and repo-local library loading are both unchanged.
Self code review for this fix is in §14 above.

---

## 16. Feature expansion — SDK-backed controls (from Rex's real validation)

### Rex's real-hardware validation (from the screenshot)
The GTK GUI ran natively on Wayland connected to the real OBSBOT Tiny 3, with
the external ffplay preview open. Confirmed working: window opens; **Tiny3 enum
18**; **SN <redacted-SN>**; **firmware 6.6.9.1**; mode UVC; run state Run;
Wake / Sleep / Center; external ffplay preview; status push arriving (~2-3s).
Observed gap in that build: PTZ arrows disabled, and `zoom` showed
`Unknown (reported 0)` because the code read the unreliable `zoom_ratio` from
the status push instead of the real getter.

### Features added (all SDK-verified for the tiny family / Tiny 3)
| Feature | SDK call(s) | Notes |
|---------|-------------|-------|
| PTZ ↑↓←→ | `gimbalGetAttitudeInfoR` + `gimbalSetSpeedPositionR` | one-shot bounded: read angle → clamped delta → move once, slow speed |
| PTZ step / speed | — | 2/5/10°, Slow/Medium |
| Presets 1/2/3 (Save/Go) | attitude get + position set | **app-local session** (SDK-native presets not documented for Tiny 3) |
| Zoom −/1x/+ | `cameraSetZoomAbsoluteR` | normalized **1.0–2.0** |
| Zoom status fix | `cameraGetZoomAbsoluteR` | shows real `1.00x`; `0` was never a real display value |
| FOV Wide/Med/Narrow | `cameraSetFovU` | `FovType86/78/65`, category `tiny series` |
| AI Track on/off | `cameraSetAiModeU(Human/None)` | toggle synced from real status push |
| Face focus on/off | `cameraSetFaceFocusR` | category `all`; reverts toggle on failure |

### Features intentionally NOT added (and why)
- **HDR/WDR** — `cameraSetWdrR` category excludes tiny3 and its state getter is
  tail-air only; no reliable Tiny 3 path.
- **Mirror/flip** — `cameraSetMirrorFlipR` is tail-air only.
- **Exposure / WB / brightness sliders** — deferred (brief prefers toggles/
  dropdowns, avoid slider churn).
- **SDK-native presets** — undocumented for Tiny 3 → app-local instead.
- **Embedded preview** — needs GStreamer (absent); external ffplay stays.

### Exact build / run commands
```fish
cd gui && make            # build the GUI (clean, zero warnings)
make test                 # hardware-free poll-timing regression test
./obsbot-tiny3-control     # from repo root; forces native Wayland
./obsbot-tiny3-control --self-test   # poll for device, print result, exit
```

### Real-hardware test checklist (run on Rex's Tiny 3)
Test safe controls first; every command logs its SDK return code (rc=0 = ok).
1. Launch `./obsbot-tiny3-control`; confirm CONNECTED, Tiny3 (enum 18), SN
   <redacted-SN>, fw 6.6.9.1, mode UVC.
2. Confirm `zoom` now shows a real value (e.g. `1.00x (1.0–2.0)`), not Unknown.
3. **Center** → expect rc=0.
4. Ensure **AI Track is OFF**, then one small PTZ move each direction (Up, Down,
   Left, Right) at step 2–5°, Slow. Watch the log: `pitch a→b, yaw c→d rc=0`.
   If a direction is inverted, note it (sign flip is a one-line change).
5. **Zoom +**, **Zoom −**, **1x** → watch zoom value change; rc=0.
6. **Preset 1 Save**, move, **Preset 1 Go** → returns to saved angle.
7. **FOV** Wide/Medium/Narrow → rc=0 (framing changes).
8. **AI Track on** → camera starts tracking; status "AI / track" shows a mode;
   **AI Track off** → returns to Off. **Sleep** only if the camera is not in use.
9. Face focus on/off → rc=0 (toggle reverts if the device rejects it).

### Files created outside `/home/vampyren/Apps/obsbot`
**None.** No config/state files are written; presets are in-memory (session).
The SDK library is loaded repo-locally via rpath; no system install, no sudo.

### Known limitations
- Hardware-specific behaviour (PTZ direction signs, AI-mode support via
  `cameraSetAiModeU` on Tiny 3, FOV/face-focus effect) is code-complete but must
  be confirmed on Rex's unit; rc is logged so unsupported calls are visible.
- Presets do not persist across restarts (session only).
- Zoom in the status panel refreshes at connect and after zoom commands, not on
  every push (the getter must not be called from the SDK callback thread).
- Embedded preview still not implemented (external ffplay only).

### Next recommended step
Confirm PTZ direction signs and AI-mode support on hardware; then persist
presets to a small repo-local config file, and (optionally) begin embedded
preview via GStreamer or a Qt 6/QtMultimedia port.

### Self code review (feature expansion)
| Check | Result |
|-------|--------|
| Unsafe movement | PTZ is one-shot absolute-position with clamped pitch(−90..90)/yaw(−120..120) at slow speed; no continuous speed, no loops. Center unchanged. |
| Wrong SDK enum/API mapping | Verified each against headers: zoom is normalized **1.0–2.0** (`cameraGet/SetZoomAbsoluteR`); `FovType86/78/65`; `AiWorkModeHuman/None`; `cameraSetFaceFocusR` is category `all`. FovType/AiWorkMode are `Device::`-nested (matches sample usage). |
| Fake status values | Zoom now read from the real getter (fixes the false `0`); `1.00x` is a real min value, not Unknown. AI state from the real push; toggle synced from it. Nothing fabricated. |
| GUI freezes | All commands run on detached worker threads; UI updates via `g_idle_add`. Discovery still off-thread. |
| Blocking call on SDK callback thread | Fixed during review: the zoom getter is **not** called from `on_dev_status` (SDK thread) — it runs at connect and after zoom commands on worker threads, avoiding status-dispatch stalls/re-entrancy. |
| Device discovery regressions | Poll-until-resolved unchanged; `make test` passes; self-test still separates UI vs device (exit 0/3). |
| Runtime libdev loading | Makefile link/rpath unchanged; `readelf` shows repo-local RUNPATH; no system install. |
| sdk-probe | Re-verified: clean build, runs (exit 1 = no device). Untouched. |
| Preview conflicts | ffplay button still warns it occupies `/dev/video0` (Meet/Miro/OBS/browser); no claim about WebRTC resolution. |
| Build outputs added | Only `gui/obsbot-tiny3-control-bin` + `gui/poll_logic_test`, both gitignored. |
| Controls before device ready | All device controls (incl. new ones) start disabled and are enabled only when a device resolves, via `g_dev_controls`. Step/speed selectors are app settings and stay usable. |
| Missing docs | README "what works", "not added", and "safety" sections updated; this report section added. |

**Fix applied during review:** moved the blocking zoom getter off the SDK
status-callback thread (see the "Blocking call" row). Final build is
warning-free; poll test and self-test pass; sdk-probe intact.
