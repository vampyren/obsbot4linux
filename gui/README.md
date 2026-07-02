# OBSBOT Tiny 3 Control — GUI PoC (developer note)

A small native Linux control panel for the OBSBOT Tiny 3, built on the validated
SDK integration in `../sdk-probe`. **This is a PoC, not the final app.**

## Toolkit: GTK3 (and why not Qt6)

The goal preferred **Qt 6/QML**. In this build environment Qt 6 is **not
available** and neither is Qt 5 or `cmake`:

```
no qmake / no qmake6, no /usr/lib/.../cmake/Qt6*, Qt6Core pkg-config: no
no Qt5Widgets, no cmake
```

Rather than pull in a large toolchain (building Qt from source / bundling it is
out of scope for a small PoC), I used the smallest practical native alternative
that **is** present and validatable here:

- **GTK3 `3.24.52`** — native Linux, has a **Wayland** backend, C API links
  cleanly from C++, and integrates directly with the C++ SDK.

If you have Qt 6 on your machine and prefer it, this PoC is small enough to port;
the SDK-facing logic (discovery, status decode, safe commands) moves over
unchanged. That is the recommended next step if a richer UI / embedded video is
wanted.

## Dependencies

| Need | Package (Debian/Ubuntu) | Package (Arch) |
|------|-------------------------|----------------|
| Compiler | `g++` | `gcc` |
| GTK3 dev | `libgtk-3-dev` | `gtk3` |
| pkg-config | `pkg-config` | `pkgconf` |
| Optional preview | `ffmpeg` (for `ffplay`) | `ffmpeg` |

The OBSBOT SDK itself is repo-local (`../libdev_v2.1.0_8`) — **no global
install, no sudo**.

## Build

```fish
# fish, bash, zsh — all the same:
cd gui
make
```

Or from the repo root, the wrapper builds on first run:

```fish
./obsbot-tiny3-control
```

## Run

```fish
./obsbot-tiny3-control              # repo-root launcher (recommended)
# or directly:
cd gui && ./obsbot-tiny3-control-bin
# options:
./obsbot-tiny3-control --wait-ms 10000  # override discovery timeout (default 6000)
./obsbot-tiny3-control --self-test      # build UI + poll for a device, then exit
```

Discovery runs on a **background thread**, so the window appears immediately and
never freezes while the SDK enumerates USB. It **polls** `getDevList()` (every
200 ms, default timeout **6000 ms**) until a device is *resolved* — present in
the list **and** with its SN populated — rather than taking one fixed sample.
This fixes a race where the SDK had read the device but not yet published it,
producing a false "NO DEVICE". Controls are enabled only once a device resolves.

### `--self-test` contract

`--self-test` builds the UI, runs one bounded discovery pass, and exits. It
prints the **UI result and the device result separately**; the exit code
reflects the **device**: `0` = found, `3` = none by timeout (UI still OK).

```
[self-test] UI: OK
[self-test] device: FOUND product=Tiny3 SN=<redacted-SN> fw=6.6.9.1 enum=18   # exit 0
```

The timing logic has a hardware-free regression test: `make test`.

### Wayland (native, not X11/XWayland)

The wrapper exports `GDK_BACKEND=wayland`, so the app runs on the **native
Wayland** GDK backend rather than falling back to X11/XWayland (GTK would
otherwise auto-select when `DISPLAY` is also set). At startup the app prints and
logs the backend it actually got, and warns loudly if it is not Wayland:

```
[backend] display backend: GdkWaylandDisplay  (native Wayland ✓)
```

Confirmed on this session: `GdkWaylandDisplay` on `wayland-0` (verified against
`GDK_BACKEND=x11`, which instead yields `GdkX11Display` + a warning). To force
X11 for debugging: `GDK_BACKEND=x11 ./obsbot-tiny3-control`.

## Runtime library loading (repo-local)

The SDK `SONAME` is `libdev.so.1.0.3`. The binary embeds an `$ORIGIN`-relative
rpath, so it finds the SDK `.so` from the repo tree with no global install:

```
RUNPATH = $ORIGIN/../libdev_v2.1.0_8/linux/x86_64-release
```

`$ORIGIN` is the binary's own dir (`gui/`). Verify:

```fish
readelf -d gui/obsbot-tiny3-control-bin | grep RUNPATH
ldd gui/obsbot-tiny3-control-bin | grep dev
```

The root wrapper also exports `LD_LIBRARY_PATH` as a fallback.

## What works vs. what is placeholder

| Area | State | SDK |
|------|-------|-----|
| Device discovery (off-thread, polled until resolved) | ✅ real | `getDevList` |
| Header: name / SN / firmware / mode | ✅ real, from the device | `devName/devSn/devVersion/devMode` |
| Connection tally pill | ✅ real (searching → connected / no-device) | — |
| Wake / Sleep | ✅ real | `cameraSetDevRunStatusR` |
| Center | ✅ real (bounded reset; rc=0 on Tiny 3) | `gimbalRstPosR` |
| **PTZ ↑↓←→** | ✅ **new** — one-shot bounded (read angle → clamped delta → move once, slow speed) | `gimbalGetAttitudeInfoR` + `gimbalSetSpeedPositionR` |
| **PTZ step (2/5/10°) + speed (Slow/Med)** | ✅ new | — |
| **Presets 1/2/3 (Save/Go)** | ✅ new — **app-local session** (stores angle; not SDK-native) | reuses attitude get + position set |
| **Zoom −/1x/+** | ✅ new — normalized 1.0–2.0 | `cameraSetZoomAbsoluteR` |
| **Zoom (status)** | ✅ **fixed** — real value via getter (`0` was never real; getter shows e.g. `1.00x`) | `cameraGetZoomAbsoluteR` |
| **FOV Wide/Med/Narrow** | ✅ new | `cameraSetFovU` (tiny series) |
| **AI Track on/off** | ✅ new — state synced from status push | `cameraSetAiModeU` |
| **Face focus on/off** | ✅ new — reverts on failure | `cameraSetFaceFocusR` (category `all`) |
| Run state / AI (status) | ✅ real, from status push | `DevStatus` / `AiWorkModeType` |
| Embedded video preview | ⛔ not implemented — needs GStreamer (absent) | — |
| External preview (ffplay) | ✅ real frames, separate window | ffplay on `/dev/video0` |

### Intentionally NOT added (and why)
- **HDR / WDR** — `cameraSetWdrR` `@category` is `tiny4k, tiny2, meet, tail air`
  (**not** tiny3), and its state getter is tail-air only. No reliable Tiny 3
  path → skipped to avoid a guessed feature.
- **Mirror / flip** — `cameraSetMirrorFlipR` is `tail air` only.
- **Exposure / white-balance / brightness sliders** — deferred; the brief asked
  to prefer simple toggles/dropdowns and avoid slider churn.
- **SDK-native presets** — not documented for Tiny 3, so presets are app-local
  (session) instead. Persisting them to a config file is a documented next step.
- **Embedded preview** — needs GStreamer; external ffplay stays the honest path.

## Camera safety

- Every movement command is **one-shot and bounded** — there is no continuous
  speed control anywhere.
- **PTZ** reads the current attitude (`gimbalGetAttitudeInfoR`), applies a small
  clamped delta (2/5/10°), and moves once to that **absolute** target
  (`gimbalSetSpeedPositionR`) at a slow reference speed, with the target clamped
  to pitch −90..90 / yaw −120..120. No loops, no sweeps.
  - Sign convention: up = +pitch, down = −pitch, left = +yaw, right = −yaw. If a
    direction is inverted on your unit, tell me and I'll flip the sign — the move
    stays bounded either way.
  - PTZ/preset moves assume AI tracking is **off** (with AI on, the SDK keeps the
    gimbal under AI control and the manual move returns an error rc — which is
    logged, not hidden).
- **Center** uses `gimbalRstPosR()` — a bounded reset to home.
- **Presets** are app-local: Save captures the current angle, Go moves back to it
  via the same bounded path.
- Evidence the tiny-family gimbal APIs apply to Tiny 3: `--center`
  (`gimbalRstPosR`) already returns rc=0 on Rex's unit.

## Preview honesty & conflicts

- The GUI does **not** fake preview. Embedded video is not implemented.
- The optional **Launch Live Preview** button runs `ffplay` on `/dev/video0` in
  an external window (real frames), trying 1080p MJPEG first and falling back to
  device defaults.
- **Conflict warning:** any preview **occupies `/dev/video0`** and will conflict
  with Proton Meet, Miro, OBS, or a browser using the camera. Close the preview
  before using the camera elsewhere.
- This tool controls the camera over the SDK's USB control channel. It does
  **not** change what resolution a browser/WebRTC negotiates for the video
  stream — that negotiation is separate and unaffected.
