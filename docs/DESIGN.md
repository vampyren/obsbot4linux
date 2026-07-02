# Design & Architecture

## Visual language — "console"

A flat **obsidian** dark UI with a single **coral** accent. Left icon rail
switches five pages (Control / Image / Track / Presets / Log); each page has a
slim top bar (page title + connection pill). Glass panels (translucent fill,
hairline borders that brighten on hover, soft radius), mono type for all chrome
(labels, metrics, log) and a system sans for reading prose. Status is carried by
coloured dots / pills, never emoji. The only persistent motion is *truthful live
signals* (the LINK heartbeat, the discovering scan rings) — nothing decorative
loops.

Design tokens live in `qt/qml/Theme.qml` (a QML singleton): colours, spacing
(4px base), radii, and motion timings. The full original design handoff (an
interactive HTML mockup + spec) is preserved under
[`design/`](design/) for reference.

## Software architecture

```
QML (console UI, qt/qml/)
   │  binds to  (context property `cam`)
   ▼
CameraController        (GUI thread) — Q_PROPERTYs, invokable actions, settings, honesty logic
   │  owns / queued calls
   ▼
CameraWorker            (worker QThread) — every blocking SDK call, serialized
   │  uses
   ▼
libdev.so (OBSBOT SDK)
```

- **`CameraController`** (GUI thread): the single QML-facing object. Mirrors
  device state into `Q_PROPERTY`s that QML binds to, forwards user actions to the
  worker as *queued* calls, persists settings/presets, owns the ffplay preview
  process, and holds the AI-toggle honesty logic.
- **`CameraWorker`** (worker `QThread`): performs **every** blocking SDK call, so
  the UI never freezes. It never touches QML/GUI objects — it only emits Qt
  signals delivered to the controller via queued connections. The SDK's status
  and plug/unplug callbacks hop onto this thread before doing any work.
- **`Settings`**: plain JSON persistence (QtCore only), so it's trivially unit-
  testable (`ctest`).

### Why a worker thread

The OBSBOT SDK calls are blocking (gimbal moves take hundreds of ms). Running
them on the GUI thread would freeze the window. Marshalling results back as
queued Qt signals also gives a clean, deterministic shutdown: on teardown the
controller disables the status callback, releases the device, then quits and
joins the thread — no detached threads, no use-after-free.

## Principles

1. **Honesty over feature count.** A control exists only if it maps to a real,
   verified SDK call. Unverified/absent features are shown disabled *with the
   reason*. No fake video, no invented status.
2. **Safety on motion.** PTZ velocity (hold-to-move) is gated behind four safety
   stops (release / window-blur / SDK error / dead-man timeout); every move is
   bounded and clamped. Auto preset moves are delayed so the gimbal's power-on
   self-centering finishes first.
3. **Single source of truth.** Device state comes from the SDK status push;
   optimistic UI updates are guarded by a pending window so a status push can't
   make a toggle "lie" mid-command.
4. **No surprises.** Anything that moves the gimbal automatically (startup preset,
   return-after-AI-off) is explicit opt-in.

## Repository layout

```
qt/            Qt 6 / QML app (src/ C++ backend, qml/ UI, packaging/ AppImage, tests/)
gui/           Original GTK proof-of-concept (kept for reference)
sdk-probe/     Tiny CLI that verifies SDK discovery/identity
sdk/           OBSBOT SDK — supplied by you, git-ignored (see docs/INSTALL.md)
docs/          This guide, install guide, design handoff, screenshots
docs/dev/      Development log / engineering notes
obsbot4linux   Repo-root launcher (build + run)
```
