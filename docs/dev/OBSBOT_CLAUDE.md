# OBSBOT Tiny 3 → Qt 6 / QML port — report

Port of the working GTK PoC to a native Qt 6 / QML "Command Center" app, built
from the design handoff in `obsbot-tiny3-linux-design/`. Fixes Rex's manual-test
findings and the `CODE_REVIEW.md` issues. Embedded preview intentionally not done
in this pass.

---

## Build / validation status (read first)

The Qt app was **compiled and tested clean in a Linux VM against Qt 6.8.3.** Full
compile-debug loop done here — not deferred to Rex:

- ✅ CMake configure clean (no warnings)
- ✅ Build clean: **0 compiler warnings** with `-Wall -Wextra` across all C++
- ✅ QML compiles (qmlcachegen) **and loads at runtime with no errors** — the full
  tree (Main + 5 pages + every component) instantiates headless (offscreen)
- ✅ `ctest` — settings/preset persistence round-trip passes (Rex #7)
- ✅ `--self-test` — SDK discovery runs and shuts down cleanly ("destroy devices
  object successfully"), exit 3 (no camera in the VM) — validates the #1/#2/#3/#9
  teardown path end to end
- ✅ `sdk-probe`, `poll_logic_test`, and the GTK reference still build
- ⏳ **Final hardware validation on Rex's CachyOS PC** (Tiny 3 attached): expect
  `--self-test` to report enum 18 / SN <redacted-SN> / fw 6.6.9.1 and exit 0

**Sandbox note (not the official path):** the VM had no root/no distro Qt, so
Qt 6.8.3 + cmake were fetched into a user dir via `aqtinstall`/pip **only to run
the compile loop**. The shipped app builds with standard **distro packages**
(below); no vendored Qt, no `aqtinstall` for users.

---

## Files changed / added

New tree beside the **untouched** GTK PoC (`gui/`) and `sdk-probe/`:

```
obsbot-tiny3-command-center          # repo-root launcher (cmake build + Wayland + rpath)
qt/CMakeLists.txt                    # qt_add_qml_module, repo-local libdev link + rpath
qt/README.md                         # build/run/deps/architecture/packages
qt/src/  main.cpp
         CameraController.{h,cpp}    # GUI-thread, QML-facing state + actions + persistence
         CameraWorker.{h,cpp}        # QThread, ALL blocking SDK calls
         Settings.{h,cpp}            # repo-local JSON persistence
qt/qml/  Main.qml  Theme.qml (singleton)
qt/qml/components/  ActionButton GlassPanel KeyValue LogView NavRail PtzPad RingGlyph
                    SectionLabel Segmented StatusPill Stepper ToggleChip Viewfinder
qt/qml/pages/       ControlPage ImagePage TrackingPage PresetsPage LogPage
```

Untouched: `gui/` (GTK reference), `sdk-probe/`, vendored SDK, existing reports.

---

## New Qt / QML file structure & architecture

```
QML (Penelope console UI)
   │  binds to  (context property `cam`)
   ▼
CameraController        (GUI thread) — Q_PROPERTYs, invokable actions, settings, AI honesty
   │  owns / queued calls
   ▼
CameraWorker            (worker QThread) — every blocking SDK call, serialized
   │  uses
   ▼
libdev.so (OBSBOT SDK)
```

- The worker **never** touches QML/GUI objects; it only emits Qt signals delivered
  to the controller via queued connections.
- The SDK status callback (`setDevStatusCallbackFunc`) and plug/unplug callback
  (`setDevChangedCallback`) both hop onto the worker thread before doing work.
- Shutdown is deterministic: disable status callback → release device → `close()`
  the SDK → `thread.quit()` → `thread.wait()`. No detached threads, no raw widget
  pointers, no post-loop use-after-free.

---

## Build command

```sh
./obsbot-tiny3-command-center                    # configures + builds (cmake) then runs
# or manually:
cmake -S qt -B qt/build -DCMAKE_BUILD_TYPE=Release
cmake --build qt/build -j
```

The build links the vendored `libdev_v2.1.0_8/linux/x86_64-release/libdev.so` with
an rpath to that directory — no global SDK install, no sudo.

## Run command

```sh
./obsbot-tiny3-command-center                    # preferred (Wayland + LD_LIBRARY_PATH + cd repo root)
./qt/build/obsbot-tiny3-control-qt --self-test   # headless: exit 0=found, 3=none, 2=init-error
./qt/build/obsbot-tiny3-control-qt --help
```

## Dependencies

Qt 6.5+ and CMake. Official install is via **distro packages** (the app was
compiled against Qt 6.8.3 in the VM, but nothing is Qt-version-specific ≥ 6.5):

**Arch / CachyOS (Rex's machine):**
```sh
sudo pacman -S --needed cmake qt6-base qt6-declarative qt6-wayland
# optional: ttf-jetbrains-mono   (chrome font; falls back to system mono)
# optional: ffmpeg               (external ffplay preview button)
```
`qt6-declarative` provides QtQuick, QtQml and QtQuick.Controls (Basic style);
`qt6-wayland` provides the native Wayland platform plugin.

**Ubuntu / Debian (reference):**
```sh
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-window qt6-wayland
```

## Where settings / presets are saved

Per-user XDG path (so it works when installed from an AppImage, not just from the
repo): `~/.config/obsbot-tiny3-control/obsbot-tiny3-control.json`. Override with
`OBSBOT_TINY3_CONFIG=/path`. Persists preset 1–3 names + pan/tilt/zoom/FOV, move
step, speed, FOV choice, framing sub-mode, startup preset, gesture, and the
"return after AI off" flag (UI restores on relaunch).

## Packaging (AppImage)

A self-contained AppImage for "download and run" (`qt/packaging/`): coral ring
icon (SVG + PNGs), `.desktop`, CMake `install()` rules, and `build-appimage.sh`
(builds → AppDir → bundles Qt + `libdev.so` → linuxdeploy + Qt plugin → AppImage).
Ships the **xcb** platform (runs on KDE + GNOME via X11/XWayland); native Wayland
is opt-in (`WITH_WAYLAND=1`). Targets modern glibc only.

**Prebuilt + validated:** `dist/OBSBOT_Tiny3_Control-x86_64.AppImage` (~41 MB).
Verified in a clean environment: the full GUI loads headless (offscreen) from the
bundled QML with no errors, and `--self-test` runs discovery + clean shutdown
against the bundled `libdev.so`. Bundles xcb + wayland + offscreen platforms, so
it runs on KDE and GNOME. (Assembled in the headless VM by staging the few X11/xcb
libs it lacks — `libxcb-cursor.so.0` etc.; on a real desktop `build-appimage.sh`
needs no staging.) Built against current glibc — fine for modern rolling distros;
rebuild on-target if the target glibc is older.

---

## What was implemented from the design handoff

- Penelope console visual language: obsidian base, glass panels (hairline borders,
  no hard shadows), single coral accent, mono chrome + sans prose, status via
  dots/pills, **no emoji**. Exact design tokens in `Theme.qml`.
- Left icon rail (Control / Image / Track / Presets / Log) with active coral
  edge-bar + tint and a bottom LINK heartbeat reflecting connection.
- Per-page top bar (overline + title) + connection pill; **SIM control dropped** —
  connection state is derived from the SDK.
- All **5 pages**; Control keeps the viewfinder pinned/always-visible while only
  the right status+controls column scrolls.
- **4 connection states** in the viewfinder: live (grid + LIVE pill + readout),
  discovering (scan rings), disconnected (Rescan), asleep (dim + Wake).
- Preview = GStreamer/ffplay **placeholder + external ffplay** affordance; no fake
  feed; viewfinder is a single surface with overlays layered on top, ready to host
  a real `VideoOutput`/GStreamer sink later.

## What was fixed from Rex's findings

1. **PTZ inversion** — both axes negated in `CameraWorker::cmdNudge`.
2. **Flat buttons** — `ActionButton`/`ToggleChip`/PTZ arrows show hover, pressed
   (scale), loading (pulsing dot from the real SDK rc), success/error (emerald/rose
   border flash), disabled (dimmed).
3–5. **Step / Speed / FOV** — segmented controls with valid initial values
   (2°/5°/10°, Slow/Medium, Wide/Med/Narrow) — no broken dropdowns.
6. **Zoom** — re-read after every ±/1x and on each status push; STATUS shows the
   real value (no longer stuck at 1x).
7. **Presets persist** across restart (JSON).
8. **AI honest & safe** — see below.

## What was fixed from CODE_REVIEW.md

- **#1/#2/#3/#9 shutdown lifecycle** — deterministic worker-thread teardown; status
  callback disabled; no detached workers touching destroyed state.
- **#4 AI-owns-gimbal honesty** — manual PTZ/Center/preset-Go are **blocked** with a
  clear "AI tracking owns the gimbal" log when AI Track is on; never a fake `(ok)`.
- **#5/#7 AI toggle** — revert-on-failure; tracking synced from the raw `ai_mode`
  (decode never blanks; `AiWorkModeSwitching=6` handled).
- **#6 hot-unplug** — `setDevChangedCallback` marks the device lost and disables
  controls.
- **#8 status-push vs in-flight toggle** — pending/debounce suppression window.
- **#10** 1x zoom reset sets 1.0 even if the getter fails.
- **#12** `--help` says `--wait-ms` default 6000.
- **#13** FOV has a valid initial index (persisted).
- **#14** dead `zoom_raw` removed.

(The GTK `gui/` app is left unchanged as reference; the findings were resolved by
building the Qt app correctly rather than patching GTK.)

## Capability gating (honesty)

**Wired (verified SDK calls):** PTZ, zoom, FOV, wake/sleep, AI Track
(`cameraSetAiModeU` Human), Face focus (`cameraSetFaceFocusR`), Gesture control
(`aiSetGestureCtrlIndividualR`), image Brightness/Contrast/Saturation/Sharpness
(`cameraSetImage*R`, SDK range 0–100, live values read on connect), and HDR
(`cameraSetWdrR`) **gated on the device-reported `hdr_support`** for the current
mode (`cam.capHdr`) — disabled with a note when the device says it's unsupported.

**Still gated OFF** (no verified Tiny 3 setters): white balance, color temp,
exposure, and advanced tracking (framing/sensitivity/motion speed/zone) — shown
disabled with a hint, never faked.

## Post-hardware feedback (Rex, real Tiny 3)

- **Face tracking regression FIXED.** The SDK has no separate face-tracking work
  mode; `cameraSetFaceFocusR` is face *autofocus*, independent of AI Track. The
  first Qt cut made "Face" mutually exclusive with "Track", so selecting Face
  turned tracking off (gimbal stopped) — that's what broke. **AI Track and Face
  focus are now independent toggles** (as in the GTK PoC), can run together, and
  face state syncs honestly from `tiny.face_auto_focus`.
- **Gesture control** wired (was gated).
- **Image tweaks** (brightness/contrast/saturation/sharpness) wired in the Image
  tab (was gated).
- **HDR** wired, gated on device-reported support.

## Post-hardware feedback round 2 (Rex)

All VM-rebuilt clean (0 warnings, ctest pass, headless QML load OK):

1. **Joystick puck now works** — the PTZ puck is draggable; on release it issues
   **one bounded nudge** in the dominant direction (still STEP mode, no continuous
   motion), then springs back. Arrows still work too.
2. **Startup preset** — new persisted "Load on startup: None/P1/P2/P3" setting
   (Presets page). On connect, the app moves to the chosen saved preset (explicit
   opt-in; only saved slots selectable).
3. **Real face tracking** — the SDK has no face-track *mode*; the true equivalent
   is **AI Track + Close-up framing** (`cameraSetAiModeU(Human, AiSubModeCloseUp)`),
   which frames tight on the face and keeps the LED blue. Framing (Normal/Upper/
   Close-up) is now **wired** on the Tracking page. `cameraSetFaceFocusR` is only
   autofocus and never turns the LED blue — the UI now says so explicitly.
4. **Gesture + labels** — the "reported" line was the AI *tracking mode* (not
   gesture); relabeled "tracking mode". Gesture is wired
   (`aiSetGestureCtrlIndividualR`) and its SDK rc is logged; a note explains it
   often needs AI Track on and to check the log if unsupported on the firmware.
5. **Image sliders** — restyled with a coral filled track + coral handle and a
   prominent value pill that highlights while dragging; added **Reset defaults**
   (sets brightness/contrast/saturation/sharpness to 50).
6. **HDR "current mode"** — clarified: HDR support (`hdr_support`) depends on the
   current **video mode (resolution/framerate)**; it's typically only available at
   1080p. The note now reads "not available at the current video mode".
7. **fps in status** — added **video fps** to the STATUS panel (from `tiny.fps`).
   **Resolution/framerate are not SDK-controllable** for the USB webcam feed —
   the SDK only sets resolution for the device's *recording/NDI/RTSP* streams, not
   the UVC output, which is negotiated by whatever app opens the camera (browser/
   OBS/ffplay). So there's no honest "set resolution" control to add; fps is shown
   because the device reports it (0 until something is streaming).

## Post-hardware feedback round 3 (Rex, real hardware testing after the AppImage)

All VM-rebuilt clean (0 warnings, ctest pass, headless QML load OK), and the
AppImage was rebuilt with these changes.

1. **Joystick now genuinely continuous (hold-to-move).** Round 2's puck only did
   one nudge per drag-release, which read as "not really a joystick." It's now
   real VELOCITY mode: hold and drag → the camera moves continuously toward the
   deflection via `gimbalSpeedCtrlR`; release → stops instantly. Implemented with
   all **four required safety stops** from the design spec: stop-on-release
   (immediate, unconditional on drag end), stop-on-window-blur (`Main.qml` calls
   `cam.gimbalStop()` when the window deactivates), stop-on-error (the worker
   auto-stops on any non-OK rc), and a deadman timeout (worker auto-stops if no
   fresh command arrives within ~400 ms — covers a stalled UI thread). Arrows are
   unchanged (still one bounded STEP nudge per tap).
2. **Startup preset confirmed working** (per your report) — no change needed.
3. **Root cause found for #8/#9 (Close-up did nothing / FOV "zoomed in and out").**
   The SDK header documents `cameraSetAiModeU`'s second parameter — the one
   round 2 used for Framing (Normal/Upper/Close-up) — as **`@category tiny2`**
   only. It is not a documented Tiny3 feature; sending it on Tiny3 produced
   erratic behavior instead of real framing, which is exactly what you saw. The
   Framing control has been **removed**. In its place: `AiWorkModePortraitTrack`
   (enum 14) is separately documented as **"Portrait tracking for tiny3"** — a
   genuine Tiny3-specific mode. The Tracking page now has a **Face Track** toggle
   using that mode; it shares the device's single `ai_mode` field with AI Track,
   so the two are mutually exclusive (turning one on turns the other off — this
   is enforced by the SDK itself, not just the UI).
4. **"Return after AI off" — was UI-only, actually wired now.** The toggle existed
   but nothing read it. Now: turning AI Track or Face Track **on** captures the
   current pitch/tilt/zoom just before tracking engages; turning it **off**
   (confirmed by the device) moves back to that captured position if the toggle
   is on. This is a genuine bug fix, not a hardware limitation.
5. **Panel text overlap fixed.** "Load on startup" and "Return after AI off" had
   a hardcoded `implicitHeight: 56`; when the description text wrapped to 2 lines
   it overflowed that fixed height and visually collided with the control next to
   it. Both panels now compute height from their content (title + description +
   control stacked vertically), matching the pattern used elsewhere in the app.
6. **"+ Save current position" button contrast fixed.** It used a flat light-pink
   fill (`accentSoft`) behind white text — low contrast, looked washed out. The
   primary button now uses a real two-stop gradient (accent → accentDeep, one
   shade darker still when pressed) with a matching darker border, and the same
   fix applies to every primary-variant button in the app.
7. **Default window size increased** (1180×748 → 1320×940, minimum 1040×780) so
   the Control page's Presets rows are visible without scrolling in the common
   case; a scrollbar was also added to the Controls panel so scrolling is visibly
   possible if the window is ever resized smaller.
8. **Resolution/fps in status.** Confirmed `video fps` was already showing in
   your screenshot. Added an explicit **`resolution: not reported by SDK`** row
   so its absence reads as a known SDK limitation, not a missing feature — the
   header has no width/height field anywhere in the status struct, and the
   resolution/framerate *setters* that do exist are for the device's
   recording/NDI/RTSP streams, not the UVC webcam feed (that's negotiated by
   whatever app opens the camera).

**Honest non-fix — Face focus (#4 recurrence).** I re-checked the code path
(`cameraSetFaceFocusR`, `@category all` per the SDK header) and found no bug —
it should be universally supported and the wiring is correct. My best guess is
that this call only biases autofocus toward a face and never moves the gimbal or
changes an LED, so in a normal single-subject, already-in-focus desk shot there
may be nothing visible to see even when it's working. **Please check the Log
page for the exact line when you toggle it** (`face focus on rc=…`) — if `rc=0`,
the SDK accepted it and the feature is working as subtly as documented; if
non-zero, tell me the rc and I'll dig further.

## Post-hardware feedback round 4 (Rex)

1. **Startup preset was unreliable (real bug, now fixed).** It fired only on
   *connect*, and even then the move was **overridden by the gimbal's power-on
   self-centering** (which runs ~1-2s after the camera comes on) — so the auto-
   load landed centered while a manual P1 later worked. Now it (a) also fires on
   **wake** (Sleep→Awake edge), and (b) is **delayed ~1.6s** so the centering
   finishes first. Same delay applied to the AI-off return.
2. **"Return after AI off" now goes to your preset.** It was returning to a
   *captured position* (my design), not a preset — and the toggle was easy to
   miss. Replaced with an explicit **"After AI off, go to: None/P1/P2/P3"**
   selector that reuses the working preset-recall path. (Snapshot machinery
   removed.)
3. **Face Track removed.** enum 14 ("PortraitTrack") is *portrait-orientation*
   tracking, not face tracking — on a landscape setup the device accepted it
   (terminal showed `cameraSetAiModeU 22 successfully`) then reverted, which is
   the "turns on then goes dark" behavior. **AI Track is the real, working
   face/person tracking** (LED blue, follows you). One tracking control now, not
   two confusing ones.
4. **Face focus consolidated + honestly labeled.** It's the same control on both
   pages (calls `cameraSetFaceFocusR`), it does NOT require AI Track, and it's
   autofocus-only (no gimbal/LED change) so its effect is subtle. Removed the
   duplicate from the **Control page**; kept one on the **Track page** relabeled
   **"Face autofocus"** with an honest description. (Still no code bug found — if
   it truly does nothing on the unit, the Log page rc will tell us.)
5. **Preset Go buttons no longer clip.** Control-page preset rows are compact now
   (Pn + summary + a single **Go**; Save/rename/clear live on the Presets page),
   and the controls column leaves room for the scrollbar so nothing sits under
   it.
6. **Preview resolution — you can now pick it and see it.** Added a resolution
   selector (**1080p30 / 1080p60 / 720p60 / 4K30**) that sets what the **ffplay
   preview** requests from the camera, shown in STATUS as `preview res`. This is
   the one honest sense of "set resolution": it's the ffplay/v4l2 capture mode
   the device negotiates for that preview — the SDK still can't set the UVC feed's
   resolution for other apps.

All VM-rebuilt clean (0 warnings, ctest pass, headless load of all pages OK); the
AppImage was rebuilt with these changes.

## Post-hardware feedback round 5 (Rex)

1. **ffplay preview lingered after close (real bug) → managed process.** The
   preview was launched *detached*, so it kept running after the app closed —
   holding /dev/video0 and printing "/dev/video0: error while seeking" (that's
   the "hanging in background"). It's now a **managed child process** that is
   terminated when the app closes and when the preview is relaunched.
2. **ffplay MJPEG/swscaler warnings silenced.** "EOI missing, emulating" and
   "deprecated pixel format" are harmless per-frame warnings from the camera's
   MJPEG stream; raised ffplay to `-loglevel error` (+`-fflags nobuffer`) so
   they no longer spam the terminal.
3. **Face focus restored to the Control page** (kept on the Track page too) —
   both are the same control; you asked for it back on main, it fits there.
4. **Gesture detection display — NOT possible on Tiny3 (honest).** The SDK's
   event-notify callback is documented "only for tail air" and there's no
   detected-gesture field in the Tiny3 status struct, so the app can enable
   gesture control (works) but cannot know *which* gesture was recognized. No
   fake indicator added.
5. **Resolution change now reloads the preview.** Changing the resolution while
   ffplay is open closes and relaunches it at the new mode (ffplay can't switch
   mid-stream). It reopens as a fresh external window.
6. **Preview resolution row overlap fixed.** The "PREVIEW" side-label overlapped
   the four wide options; the label is now on its own line and the selector gets
   the full panel width.
7. **Preset Save on the Control page: intentionally removed** — Control shows a
   quick **Go** only; Save/rename/clear live on the Presets tab (as you're OK
   with).
8. **Scrollbar auto-hides** — the controls-panel scrollbar now fades out when
   idle and appears only while scrolling or hovered.

## What remains / intentionally not implemented

- **Embedded in-app preview** — the agreed next big change (currently external
  ffplay only).
- Gesture *detection* display (which gesture was recognized) — not exposed by the
  Tiny 3 SDK.
- White balance / color temp / exposure and advanced tracking (sensitivity/
  motion speed/zone) — still gated OFF (no verified Tiny 3 setters).
- Face autofocus effect is inherently subtle (autofocus bias, no gimbal/LED) —
  kept but honestly labeled; pending the Log rc to confirm the device accepts it.
- Frameless/CSD title bar, lucide SVG icons, bundled JetBrains Mono — cosmetic
  follow-ups (standard window decorations used for reliability now).

## sdk-probe status

Unchanged and **rebuilds cleanly** on the sandbox (`make -C sdk-probe`). SDK
discovery/linkage intact.

## Self-test status

`--self-test` runs headless (offscreen platform), performs discovery once, and
exits 0 = device found, 3 = no device, 2 = init error (fixes CODE_REVIEW #11: a
headless run no longer returns 0 for "no device"). **Exercised in the VM:** it ran
discovery, timed out with no camera, tore the SDK down cleanly, and returned 3 as
designed. On Rex's machine it should report Tiny3 enum 18, SN <redacted-SN>,
fw 6.6.9.1 and exit 0. A hardware-free `ctest` (settings/preset persistence
round-trip) also passes.

---

## Self code review

Static pass (pre-build) found and fixed:
- **`std::max` missing `<algorithm>`** in `main.cpp` → added.
- **`m_aiInFlight` used but undeclared** in the controller → declared.
- **Viewfinder dim-wash drawn over the Wake button** → moved below the overlays.
- **`ControlPage { logModel: ... }`** bound a non-existent property → removed.
- **LogView delegate** used fragile `parent.parent` chains → rewritten with ids.

VM compile-debug loop (Qt 6.8.3) found and fixed:
- **Fractional `font.pixelSize` (8.5 / 11.5 / 12.5)** — `pixelSize` is an *int*
  property; qmlcachegen accepted it but the runtime loader rejected it
  (`NavRail.qml: Invalid property assignment: int expected`). Rounded all 9
  occurrences to ints. This is the class of bug the VM loop exists to catch.
- **`QTP0004` policy warning** for the QML subdirectory → flattened all QML into
  `qml/` and set the policy via the correct `QT_KNOWN_POLICY_QTP0004` guard, so
  configure is warning-free and type resolution is deterministic on Qt 6.8→6.10+.
- Enabled `-Wall -Wextra` → **0 warnings** in our C++.

Post-hardware round (face fix + gesture/HDR/image), reviewed + VM-rebuilt clean:
- **Face/Track decoupled** — verified selecting Face no longer issues an AI-off
  command; the two toggles share only the `m_aiPending`/`m_aiInFlight` suppression
  (each setter sets both optimistic targets so neither clobbers the other).
- **New SDK calls** (`aiSetGestureCtrlIndividualR`, `cameraSetWdrR`,
  `cameraSetImage{Brightness,Contrast,Saturation,Sharp}R`, and the `tiny.hdr`,
  `tiny.hdr_support`, `tiny.face_auto_focus` status fields) all confirmed against
  the headers and compile clean.
- **HDR is gated on `tiny.hdr_support`** (dynamic `capHdr`), not assumed — honest.
- **Image sliders apply on release** (not per-drag frame) to avoid flooding the
  SDK; values re-read from the device on connect so the mirror stays truthful.
- Re-ran the full suite: 0 warnings, `ctest` pass, headless QML load of all pages
  (incl. new Image/Tracking controls) with no runtime errors.

Feedback round 2 (joystick / startup preset / framing / gesture / image / fps):
- **Joystick drag** — the knob is now driven imperatively by both the DragHandler
  and the arrow `_bump`; the position binding is intentionally one-shot and the
  spring-back animation is disabled *during* drag (`Behavior … enabled: !drag.active`)
  so it doesn't fight the finger. Verified it still issues exactly one bounded
  nudge per gesture (STEP mode preserved — no velocity loop).
- **Startup preset** only fires for a *saved* slot and is explicit opt-in — no
  surprise gimbal motion. Framing re-applies only while tracking is on.
- Checked the honest boundaries: resolution/fps for the UVC feed is **not** an SDK
  control (only record/NDI/RTSP have setters) — so I added the fps *readout* rather
  than a fake control, and worded the HDR/mode note truthfully.
- Rebuilt clean (0 warnings), `ctest` pass, full headless QML load with no errors.

Round 3 (real continuous joystick / Face Track / return-to-position / layout /
button contrast) **supersedes** round 2's drag and Framing behavior above:
- **Velocity mode's four safety stops verified by inspection, not just claimed:**
  stop-on-release calls `cam.gimbalStop()` unconditionally in `onActiveChanged`;
  stop-on-blur is wired in `Main.qml` (`onActiveChanged: if (!active) cam.gimbalStop()`);
  stop-on-error is in `CameraWorker::cmdGimbalVelocity` (any non-OK rc immediately
  sends a zero-velocity stop and clears `m_velocityActive`); the deadman timer
  (`m_velocityWatchdog`, 400 ms, singleShot, restarted on every fresh command) is
  created in `init()` so it's owned by the worker thread and safely torn down with
  the rest of the worker on shutdown.
- **`cmdGimbalStop` is idempotent** (`if (!m_velocityActive) return;`) — safe to
  call from multiple paths (drag-release, window-blur, deadman, shutdown) without
  redundant SDK calls or log spam.
- **Sign convention rechecked against the existing nudge convention**, not
  invented fresh: knob dy/dx map to pitch/yaw fraction with the *same* signs
  already validated for the arrow nudges (up = −pitch, right = +yaw), so the
  joystick and arrows agree on direction.
- **`applyAiMode` funnels both AI Track and Face Track through one pending leg**
  (they share the device's single `ai_mode` field) while `setFaceFocus` still
  preserves the *other* two legs' target values on each call — re-verified this
  3-leg bookkeeping (tracking / faceTrack / faceFocus) doesn't let one toggle's
  pending window display a stale value for an untouched leg.
- **Return-to-snapshot is queued, not signal-raced:** `cmdCaptureSnapshot` is
  invoked (queued) *before* `cmdSetAi` in `setAiTracking(true)`/`setFaceTrack(true)`;
  since both land on the same worker thread, Qt's queued-connection FIFO
  guarantees the attitude read happens before the mode switches — no explicit
  synchronization needed, no risk of capturing a post-AI position.
- Removing the Framing control also removed its only caller of the `subMode`
  parameter with a non-zero value — confirmed `cmdSetAi` is now only ever called
  with `subMode=0`, consistent with the "0 = Normal case, safe for every mode"
  reading of the SDK doc.
- Rebuilt clean (0 warnings), `ctest` pass, full headless QML load with no errors;
  **the AppImage was rebuilt** with this round's changes (same host-lib-pruning
  recipe as before) and re-validated (self-test + headless load) before delivery.

Round 4 (startup-preset reliability / return-to-preset / Face Track removal /
Face focus consolidation / layout / preview resolution):
- **Startup-preset delay uses the context-object `QTimer::singleShot(this, …)`**
  so a destroyed controller cancels the pending fire (no use-after-free), and the
  lambda re-checks `connected() && !m_aiTracking` at fire time (state may have
  changed during the 1.6s wait).
- **Wake-edge detection reads `prevRun` before updating `m_runState`** and only
  fires on Asleep→Awake — verified it does NOT double-fire with the on-connect
  path (connect isn't a Sleep→Awake transition; first status after connect is
  Unknown→Awake, which is excluded).
- **Removed all Face Track / PortraitTrack / snapshot code** — re-grepped `qt/`
  for `faceTrack`/`aiReturnToLast`/`snapshot`/`applyAiMode`; no functional refs
  remain (only legacy comments + an unused persisted `trackFraming` settings
  field kept for backward-compatible JSON). The settings test was updated to the
  new fields and passes.
- **AI-mode pending bookkeeping simplified back to 2 legs** (AI Track + face
  focus) now that Face Track is gone — re-verified each setter preserves the
  other leg's target so a pending window can't show a stale value.
- **Return-to-preset reuses `goPreset`** (the same path a manual Go uses, already
  hardware-proven), queued after the confirmed AI-off, so it inherits the
  aiOwnsGimbal-released state and the existing clamps.
- Rebuilt clean (0 warnings), `ctest` pass, full headless QML load of all pages
  with no errors; AppImage rebuilt + re-validated.

Verified empirically (not just by reading): the full QML tree loads headless with
no errors; `cam.*` bindings all resolve; discovery + deterministic shutdown run
clean; settings/presets round-trip via `ctest`. SDK callback signatures
(`DevStatusCallback = std::function<void(void*,const void*)>`, `devChangedCallback`)
and `CameraStatus.tiny.{zoom_ratio,ai_mode,dev_status,face_auto_focus,hdr,hdr_support}`
confirmed against headers.

**Remaining risk is now narrow:** distro-Qt vs VM-Qt differences (Rex's Qt 6.9/
6.10 vs 6.8.3) and Wayland/KDE/GNOME windowing — none touch app logic. The app is
not portable as a *binary* to CachyOS (different Qt/libc); Rex rebuilds from source
with distro packages, then runs `./obsbot-tiny3-command-center --self-test`.

---

## Manual test checklist for Rex

- [ ] PTZ directions correct (up/down/left/right move the expected way)
- [ ] Every button shows pressed + loading feedback; success/error flashes
- [ ] Move-step UI clear and works (2° / 5° / 10°)
- [ ] Speed UI clear and works (Slow / Medium)
- [ ] FOV has a valid initial value and switches
- [ ] Zoom display changes after +, −, and 1x
- [ ] Presets persist after restart
- [ ] AI on/off does not lie (STATUS + toggle agree with the device)
- [ ] Manual PTZ while AI is on is blocked with a clear log line (no fake ok)
- [ ] Closing the app during/after a command does not crash
- [ ] Reopen the app and settings/presets are restored
- [ ] `--self-test` detects Tiny3 enum 18, SN <redacted-SN>, fw 6.6.9.1

**Round 3 additions:**
- [ ] Holding the joystick puck moves the camera continuously; releasing stops
      it immediately (not a single nudge)
- [ ] Switching windows/apps while holding the puck stops the motion (stop-on-blur)
- [ ] AI Track and Face Track are mutually exclusive (turning one on turns the
      other off); Face Track visibly frames tighter on the face, LED blue
- [ ] Turn on "Return after AI off" (Presets page), enable AI Track, then turn
      it off — camera returns to its pre-tracking position
- [ ] "Load on startup" panel and "Return after AI off" panel text no longer
      overlaps the control next to it
- [ ] "+ Save current position" and other primary buttons look like solid coral
      buttons, not flat/washed-out pink
- [ ] Window opens large enough to see the Presets rows without scrolling
- [ ] STATUS shows a `resolution: not reported by SDK` row (honest, not missing)
- [ ] Face focus: toggle it and check the Log page line — report the rc value
