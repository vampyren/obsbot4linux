# OBSBOT4Linux — Qt 6 / QML app

The native Qt 6 / QML application: a clean, honest control console for OBSBOT
cameras (Tiny 3 today). It grew out of the "Command Center" design handoff
(preserved under `../docs/design/`) and the original GTK PoC (`../gui/`, kept as
reference). It drives the OBSBOT SDK (`libdev`, supplied under `../sdk/` — see
`../docs/INSTALL.md`) with a controller/worker architecture and a
capability-gated control surface.

> **Build status:** compiles clean against **Qt 6.8.3** — 0 compiler warnings
> (`-Wall -Wextra`), the full QML tree loads with no runtime errors (offscreen),
> the hardware-free tests pass (`ctest` + `--self-test`), and it is validated on
> real hardware (Tiny 3, fw 6.6.9.1). The official build uses **distro Qt
> packages** (below).

## Architecture

```
QML (console UI)  ── binds to ──►  CameraController   (GUI thread)
                                        │ owns
                                        ▼
                                   CameraWorker        (QThread — ALL blocking SDK calls)
                                        │ uses
                                        ▼
                                   libdev.so (OBSBOT SDK)
```

- **`CameraController`** (GUI thread): the single QML-facing object. Mirrors device
  state into `Q_PROPERTY`s, forwards user actions to the worker as *queued* calls,
  persists settings/presets, owns the external preview process, and holds the AI
  toggle honesty logic.
- **`CameraWorker`** (worker `QThread`): performs **every** blocking SDK call. It
  never touches QML/GUI objects — it only emits Qt signals, delivered to the
  controller via queued connections. The SDK status callback hops onto this
  thread before doing anything. Shutdown is deterministic (disable callback →
  release device → `quit()` → `wait()`).
- **`Settings`**: JSON persistence (QtCore only).

See `../docs/DESIGN.md` for the full design/architecture write-up.

## Build

Requires Qt 6.5+ and CMake. From the repo root, the launcher builds on first run:

```sh
./obsbot4linux          # configures + builds (CMake) then runs
```

or manually:

```sh
cmake -S qt -B qt/build -DCMAKE_BUILD_TYPE=Release
cmake --build qt/build -j
./qt/build/obsbot4linux      # run directly (set LD_LIBRARY_PATH — see launcher)
```

The build links `../sdk/libdev_v2.1.0_8/linux/x86_64-release/libdev.so` with an
rpath to that directory — **no global SDK install, no sudo**.

## Run

```sh
./obsbot4linux           # preferred: sets Wayland + LD_LIBRARY_PATH, cd's to repo root
./qt/build/obsbot4linux --self-test   # headless discovery smoke test (exit 0=found,3=none,2=init)
./qt/build/obsbot4linux --help
```

## Dependencies

Qt 6.5+ and CMake, via distro packages:

### Arch / CachyOS
```sh
sudo pacman -S --needed cmake qt6-base qt6-declarative qt6-wayland
# optional, for the intended chrome font (otherwise falls back to a system mono):
sudo pacman -S --needed ttf-jetbrains-mono-nerd   # or ttf-jetbrains-mono
```
- `qt6-declarative` provides QtQuick, QtQml and QtQuick.Controls (Basic style).
- `qt6-wayland` provides the native Wayland platform plugin (the launcher prefers
  `QT_QPA_PLATFORM=wayland;xcb`).

### Ubuntu / Debian (reference)
```sh
sudo apt install cmake g++ \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-window \
  qt6-wayland
```

`ffplay` (from `ffmpeg`) is optional — only needed for the external preview
button; the app runs fine without it (the button is disabled + explains why).

## Tests (hardware-free)

```sh
ctest --test-dir qt/build --output-on-failure     # settings/preset persistence round-trip
./qt/build/obsbot4linux --self-test     # discovery + clean shutdown (exit 0/3)
QT_QPA_PLATFORM=offscreen ./qt/build/obsbot4linux   # loads the full UI headless
```

None of these need a camera. On a box with a Tiny 3 attached, `--self-test`
additionally reports the device and exits 0.

> **Sandbox note (not for normal users):** the dev VM used to compile this had no
> root and no distro Qt, so Qt 6.8.3 + cmake were fetched into a user directory
> via `aqtinstall`/pip **purely to run the compile-debug loop**. That is a sandbox
> workaround only — the shipped app builds with the standard distro packages above
> and does **not** vendor Qt or require `aqtinstall`.

## Settings / presets

Persisted to an XDG config file:

```
~/.config/obsbot4linux/obsbot4linux.json
```

Override with `OBSBOT4LINUX_CONFIG=/path/to/file`. Persists: preset 1–3 names +
positions (pan/tilt/zoom/FOV), move step, speed, FOV choice, startup preset,
"return after AI off" preset, preview resolution, sleep-on-exit, image params,
and the gesture toggle.

## Capability gating (honesty)

**Wired (verified SDK calls):** PTZ nudge/center and velocity hold-to-move, zoom,
FOV, wake/sleep, AI Track (`cameraSetAiModeU`), Face autofocus
(`cameraSetFaceFocusR`), Gesture control (`aiSetGestureCtrlIndividualR`), and
image Brightness/Contrast/Saturation/Sharpness (`cameraSetImage*R`, 0–100).

**Gated OFF** (no verified Tiny 3 setters, shown disabled with a hint, never
faked): HDR/WDR (the SDK's `cameraSetWdrR` is not a Tiny 3 category and
`hdr_support` reports 0), white balance, color temp, exposure, and advanced
tracking (framing/sensitivity/motion speed/zone).

## PTZ safety

Two modes are wired: **STEP/NUDGE** (arrow tap = one bounded, clamped, one-shot
move) and **VELOCITY** (draggable joystick = hold-to-move). Velocity is guarded
by four safety stops — stop-on-release, stop-on-window-blur, stop-on-SDK-error,
and a dead-man watchdog timer — plus redundant stop commands on release.
