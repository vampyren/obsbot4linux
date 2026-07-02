# Handoff: OBSBOT Tiny 3 Command Center (Linux / Qt 6 · QML · Wayland)

## Overview

A clean, single-window desktop control surface for the **OBSBOT Tiny 3** PTZ webcam on Linux, driven by the OBSBOT SDK. It replaces the busy stock OBSBOT Center layout with a focused **operator console**: a left icon rail switches between five pages (Control, Image, Tracking, Presets, Log); the Control page keeps the live viewfinder pinned while a status + controls column scrolls beside it.

This is the design chosen from an earlier round of three directions ("Console"). It is the only direction in this bundle.

---

## About the design files

The file in this bundle — `OBSBOT Command Center.dc.html` — is a **design reference created in HTML/JS**. It is an interactive prototype that shows the intended look, layout, motion, and interaction model. **It is not production code to copy.**

Your task is to **recreate this design as a native Qt 6 / QML application** (Wayland target on Linux) using idiomatic QML/Qt Quick Controls patterns and the project's own structure — not to embed or port the HTML. Where the HTML uses web-isms (flexbox, React state), map them to QML equivalents (`RowLayout`/`ColumnLayout`, `ListView`, property bindings, `states`/`transitions`).

Open the HTML in a browser to click through it while you build. Every interaction described below is live in that file.

---

## Fidelity

**High-fidelity.** Colors, typography, spacing, radii, and interactions are final and intended to be reproduced faithfully. Use the exact token values in the Design Tokens section. The one deliberate placeholder is the **video preview** (see below) — everything else is spec.

---

## Visual language (Penelope console)

- Flat **obsidian** background — no gradients on the base, no textures, no full-bleed imagery. Faint accent radial glow allowed only behind the viewfinder/identity.
- **Glass panels**: translucent white fill, 1px hairline border that brightens on hover, subtle blur, 10px radius. Elevation reads as light (soft inset highlight + faint halo), **not** hard drop shadows.
- **One coral accent** drives primary actions, active cues, focus rings, links. Everything else is greyscale on obsidian, punctuated by status tones (emerald / cyan / amber / rose).
- **Mono chrome + sans prose**: JetBrains Mono for all chrome (labels, metrics, log, status values, nav); system sans for reading prose (control descriptions).
- Status is carried by **colored dots / pills / a heartbeat glow**, never emoji. No emoji anywhere.
- Motion: **150–300 ms, ease-out** `cubic-bezier(0.16, 1, 0.3, 1)`. The only persistent motion is *truthful live signals* (the LIVE pill's pulsing tick, the rail "LINK" heartbeat, the discovering scan rings). Nothing decorative loops.

---

## Window & shell

- Frameless / client-side-decorated window (Wayland CSD), ~**1180 × 748** default, resizable. Rounded 12px corners, 1px border.
- Title bar (38px): OBSBOT ring glyph + `OBSBOT Tiny 3 Control` (mono, dim) on the left; window min/maximize/close on the right. Under Wayland use `Qt.FramelessWindowHint` + CSD or the compositor's decorations per your app convention.
- Body = left **icon rail** (70px, full height, glass, right hairline) + a main column (slim top bar + page content).

### Left rail
- Top: OBSBOT ring glyph (coral 3px ring, coral center dot, coral glow).
- Five nav items, each = 18px lucide icon + 8.5px mono label stacked, 52×50px hit area, 9px radius:
  - **Control** — `LayoutGrid`
  - **Image** — `SlidersHorizontal`
  - **Track** — `Crosshair`
  - **Presets** — `Bookmark`
  - **Log** — `Terminal`
- Active item: `--accent-tint` fill, `--accent-soft` text/icon, and a **3px coral indicator bar** (glowing) on its left edge.
- Bottom (pushed down): a `heartbeat` dot + `LINK` label; dot is emerald when connected, rose when down.

### Top bar (per page)
- Left: overline `OBSBOT · PTZ CONTROL` (coral, uppercase, tracked) + 18px page title (`Live Control`, `Image & Exposure`, `AI Tracking`, `Presets`, `Activity Log`).
- Right: the **SIM** control (prototype-only, see note) + the **connection pill**.

> **SIM control is prototype scaffolding only.** In the HTML, a dashed "SIM · Live / Scan / Off" segmented control lets a reviewer fake the connection state (connected / discovering / disconnected). **Do not ship it.** In the real app, connection state is derived from the SDK's discovery/connection callbacks. Keep the connection pill; drop the SIM box.

### Connection pill (top-right, keep)
- `CONNECTED` — emerald, static dot.
- `DISCOVERING` — amber, **pulsing** tick.
- `DISCONNECTED` — rose, static dot.

---

## Screens / Views

Screenshots for each are in `screenshots/` (01 Control, 02 Image, 03 Tracking, 04 Presets, 05 Log, 06 Disconnected, 07 Discovering).

### 1. Control (`Live Control`) — default page
**Purpose:** live monitoring + real-time PTZ / zoom / power / preset operation.

**Layout:** two columns filling the page.
- **Left (flex, min 360px) — pinned, does NOT scroll:**
  - **Viewfinder** fills the vertical space (see Preview section). Stays fixed while the right column scrolls.
  - Below it, a glass **transport bar**: `Wake / Sleep / Center` group on the left, `Zoom − [1.30x] + 1x` on the right.
- **Right (300px fixed) — scrolls independently:**
  - **STATUS panel** (glass): label→value rows in mono — `product` Tiny3 (enum 18), `SN` <redacted-SN>, `firmware` 6.6.9.1, `mode` UVC, `run state` (Active/Sleep, colored), `zoom` 1.00x (1.0–2.0), `AI / track` (Off/Track/Face, colored). Values are coral-soft; run state emerald when active / amber when asleep.
  - **CONTROLS panel** (glass): the **PTZ pad** (joystick puck + arrow ring + center), then `Step` + `Speed` selects, then `FOV` select with `AI Track` + `Face focus` sharing a row, a divider, then the **PRESETS** rows (P1–P3, each Save + Go).

> Only the right column scrolls. The viewfinder must remain visible at all times — this was an explicit product requirement.

### 2. Image (`Image & Exposure`)
**Purpose:** picture tuning. **Layout:** wide settings panel (max 560px) + a 300px reference preview thumbnail on the right.
**Controls:** Brightness / Contrast / Saturation / Sharpness sliders (0–100%); White balance segmented (Auto / Manual); Color temperature slider 2800–7500K (disabled when WB=Auto); Exposure select (Auto, −1.0…+1.0 EV); HDR toggle with description.

### 3. Tracking (`AI Tracking`)
**Purpose:** AI framing config. **Layout:** settings panel + reference preview thumbnail (preview shows the tracking reticle when active).
**Controls:** AI mode segmented (Off / Track / Face) — when Off, the rest is disabled with a hint line; Framing segmented (Close-up / Upper / Full body); Sensitivity slider; Motion speed segmented (Slow / Std / Fast); Tracking zone select (Auto / Center / Left third / Right third); Gesture control toggle.

### 4. Presets
**Purpose:** manage saved gimbal/zoom positions. **Layout:** single column, max 720px.
**Controls:** one glass row per preset — a Pn tile (coral-tinted when set), an editable **name** field, a mono position summary (`pan …° · tilt …° · 1.30x`), and `Go` / `Overwrite` / `Clear`. Empty slots read "Empty slot" with a `Save`. A `+ Save current position` button fills the next empty slot.

### 5. Log (`Activity Log`)
**Purpose:** discovery/connection/command trace. **Layout:** wide mono log panel + a 300px STATUS panel.
**Content:** timestamped lines, colored by kind — `net` cyan (discovery/polling), `ok` emerald (resolved/connected), `warn` amber, `cmd` coral (user actions), `sys` grey. Auto-scrolls to newest.

### Connection states (all pages)
- **Connected** (primary) — full UI enabled.
- **Discovering** — viewfinder shows concentric **scan rings** + `Discovering…` + `polling /dev/video0 · USB`; connection pill amber-pulsing; controls disabled.
- **Disconnected** — viewfinder shows a `video-off` glyph + `No device found` + `Rescan`; controls disabled; pill rose.
- **Asleep** (connected + run=sleep) — viewfinder dims with a moon glyph + `Camera asleep` + a `Wake` button; controls remain enabled so you can wake it.

---

## Component breakdown

| Component | Spec |
|---|---|
| **Glass panel** | fill `rgba(255,255,255,0.025)`, border `1px rgba(255,255,255,0.07)` → `0.16` on hover, radius 10px, blur 8px. |
| **Primary button** | coral gradient `#ea8b92 → #e06c75`, border `#c14b55`, white text, inset top gloss + coral bloom; height 30 (sm) / 36 (md); radius 8px. |
| **Secondary button** | translucent white gradient, `rgba(255,255,255,0.14)` border, `--fg` text. |
| **Ghost button** | transparent, dim text, washes to `rgba(255,255,255,0.06)` on hover. |
| **Toggle button (Wake/Sleep/AI/Face)** | secondary base; when active, tinted fill + colored border + colored glow in the tone that matches its meaning (Wake=emerald, Sleep=amber, AI/Face=cyan). |
| **Segmented control** | inline group in a bordered well; active segment = `--accent-tint` fill + `--accent-deep` inset ring + coral-soft text. |
| **Select** | transparent field, dim border → coral on focus, mono text, 32px. |
| **Slider** | native range styled with coral `accent-color`; label + tabular-num value above. |
| **Switch** | 44×24 pill; knob springs left/right 180ms; coral gradient + glow when on. |
| **Status pill** | uppercase mono, tone color, leading dot; `.tick.live` pulses (1.4s) for in-progress. |
| **PTZ pad** | circular field (crosshair + dashed inner ring) with a coral joystick knob; below it a 3×3 arrow grid (up/left/**center**/right/down). |
| **Preset row** | glass row: Pn tile + name input + position metric + Go/Overwrite/Clear. |
| **Log** | mono, 11.5px, 1.7 line-height, per-line color by kind, auto-scroll. |

Icons: **lucide** set, 2px stroke, no fill, 14–18px in chrome. Use `lucide` (or Qt-bundled equivalents) — do not invent icons or use an icon font.

---

## Interactions & behavior

- **Nav:** clicking a rail item swaps the page. Active state = tint + coral edge bar.
- **Wake / Sleep:** set run state; Sleep dims the viewfinder to the asleep overlay.
- **Center:** returns pan/tilt to home (0,0), clears active preset.
- **Zoom:** `−` / `+` step by 0.10 within 1.00–2.00×; `1x` resets to 1.00.
- **Step / Speed / FOV / Zone / Exposure:** select bindings.
- **AI Track / Face focus:** mutually exclusive with Off; toggling one off returns to Off. Track/Face draws the reticle on the viewfinder.
- **Presets:** Save/Overwrite captures current pan/tilt/zoom(/FOV); Go recalls; Clear empties; name is editable inline.
- **Every action appends a Log line.**
- **Transitions:** page swap and panels are instant/soft; toggles/knobs animate 150–220ms ease-out. Discovering scan-rings and the LIVE/LINK pulses are the only continuous motion.
- **Disabled propagation:** when not `connected`, all camera controls are disabled (visually dimmed); the viewfinder shows the matching empty/scan state.

### PTZ control — IMPORTANT (read before implementing)

You may present the puck **visually** as a joystick-style PTZ control (it looks and feels like a gimbal joystick — that matches the Command Center feel, and the HTML animates it that way). But the **implementation has two clearly separated modes**, and the safe one ships first:

**1. Safe default mode — STEP / NUDGE (ship this):**
- One click/tap of the puck or an arrow = **one bounded PTZ nudge**.
- Command = `current angle + clamped delta` (delta bounded by the `Step` selection and a hard safety clamp).
- **No continuous movement.** No repeat-while-held. Each interaction issues exactly one bounded move command and returns.
- This is the default and the only mode enabled until velocity mode is validated on hardware.

**2. Future / experimental mode — VELOCITY (hold-to-move) — do NOT default:**
- Movement only while the control is actively held; deflection = direction/speed.
- **Requires all of the following before it can be enabled:**
  - SDK **stop-on-release** (movement halts the instant the pointer/key releases).
  - **stop-on-window-blur** (halts if the window loses focus / compositor deactivates it).
  - **stop-on-error** (halts on any SDK error or dropped link).
  - **timeout / deadman stop** (an independent watchdog halts motion if no fresh "continue" within N ms).
- Must be gated behind an explicit setting/flag and **must not be the default until tested on real Tiny 3 hardware.**

Implement STEP mode now. Scaffold velocity mode behind a disabled capability flag with the four safety stops stubbed, so enabling it later is a config change, not a rewrite.

---

## Video preview

- The viewfinder is a **GStreamer / ffplay placeholder** for now. In the HTML it renders a stylized scene (rule-of-thirds grid, framing reticle, LIVE pill, PAN/TILT/ZOOM readout, asleep/scan/disconnected states) and an `ffplay ↗` affordance that opens an external preview window.
- **Do not fake an embedded video feed.** Ship the placeholder + external-preview affordance until a real pipeline exists.
- **Design the layout so a real embedded preview can drop in later without redesign:** treat the viewfinder as a single resizable surface (e.g. a `VideoOutput` / `QtMultimedia` sink or an embedded GStreamer widget) that fills the same pinned region. The overlays (LIVE pill, readout, reticle) should be a layer *on top of* that surface so they compose over real video unchanged.

---

## State management

Per-session (runtime) state, mirrored from the SDK where applicable:
- `conn` (connected | discovering | disconnected), `run` (awake | sleep) — **derived from SDK**, not user-set.
- `pan`, `tilt` (clamped range), `zoom` (1.0–2.0), `step`, `speed`, `fov`.
- `ai` (off | track | face), tracking config (`trackMode`, `trackSpeed`, `zone`, `sensitivity`, `gesture`).
- image config (`brightness`, `contrast`, `saturation`, `sharpen`, `wb`, `wbTemp`, `exposure`, `hdr`).
- `presets[3]` (`{ set, name, pan, tilt, zoom, fov }`), `activePreset`.
- `log[]` (`{ kind, timestamp, message }`).
- current `page` (nav selection).

Triggers: SDK discovery/connection callbacks drive `conn`/`run` and status; user actions drive the rest and each appends a log entry.

### Persistence (repo-local)
- Persist settings + presets to a repo-local JSON file: **`./config/obsbot-tiny3-control.json`**.
- Presets must store at least **name, pan, tilt, zoom, and FOV** (FOV only if the SDK/device exposes it).
- Also persist last-used step/speed and image/tracking config so the UI restores on relaunch. Load on startup; write on change (debounced).

### Capability gating
- Image / exposure / HDR / white-balance controls must be **capability-gated**: only enable a control if the Tiny 3 SDK actually exposes it. If support is unverified, render the control **disabled with a "not available on this device / SDK" hint** rather than sending unsupported commands. Query capabilities once on connect and bind control enablement to that result.

---

## SDK mapping table

Map each UI action to the corresponding OBSBOT SDK capability. **Method names below are intent labels — confirm the exact call/enum against the OBSBOT SDK headers you're linking; do not assume signatures.**

| UI action | Intended SDK capability | Notes |
|---|---|---|
| Device discovery / Rescan | start USB/UVC device enumeration | drives `discovering` → `connected`; feeds Log `net`/`ok` lines |
| Connection lifecycle | open/close device handle; subscribe status push | derives `conn`, populates STATUS (product/SN/fw/mode) |
| Wake | set run/power state → active | |
| Sleep | set run/power state → sleep | dims viewfinder |
| Center | gimbal move to home (pan 0, tilt 0) | |
| Arrow nudge / puck (STEP mode) | gimbal move by bounded relative delta | `current + clamped(step)`; single command, no repeat |
| Puck (VELOCITY mode — future) | gimbal continuous move at velocity + explicit stop | gated; needs stop-on-release/blur/error + deadman |
| Zoom − / + / 1x | set zoom absolute (1.0–2.0) | clamp to device range |
| Step / Speed | local params applied to move commands | speed maps to gimbal motion speed if SDK supports |
| FOV | set field-of-view / crop mode | capability-gated |
| AI Track | set AI mode → tracking (+ framing/sensitivity/speed/zone) | |
| Face focus | set AI mode → face | mutually exclusive with Track |
| Gesture control | enable/disable gesture recognition | |
| Brightness/Contrast/Saturation/Sharpness | set image params | capability-gated |
| White balance / Color temp | set WB mode / manual temperature | temp enabled only when WB=Manual |
| Exposure / HDR | set exposure comp / HDR mode | capability-gated |
| Preset Save/Overwrite | capture current pan/tilt/zoom(/FOV) → JSON | persisted to config file |
| Preset Go | recall stored angles → gimbal + zoom commands | |
| ffplay ↗ | launch external preview (spawn ffplay on the UVC node) | until embedded pipeline exists |

---

## Design tokens

**Color**
- Background `--bg` `#08090b`
- Panel fill `rgba(255,255,255,0.025)`; elevated hot `rgba(255,255,255,0.05)`
- Border `rgba(255,255,255,0.07)`; hover `rgba(255,255,255,0.16)`
- Text `--fg` `#f4f4f5`; dim `#a1a1aa`; dimmer `#71717a`
- Accent `--accent` `#e06c75`; soft `#ea8b92`; deep `#c14b55`
- Accent tint `rgba(224,108,117,0.13)`; ring `rgba(224,108,117,0.55)`
- Status: live `#10b981` · busy `#06b6d4` · degraded `#f59e0b` · offline `#f43f5e` · unknown `#71717a`

**Typography**
- Chrome (all UI): **JetBrains Mono** — body 13px, meta/caption 11.5px, overlines 11px UPPERCASE tracked 0.18–0.22em.
- Prose (descriptions): system sans stack — body 14–15px.
- Metrics use tabular-nums so counters don't jitter.
- Page title 18px (top bar); section overlines 11px.

**Spacing** — 4 / 8 / 12 / 16 / 20 / 24 / 32 (4px base).
**Radius** — control 8 · surface 10 · pill 999 · chip 3.
**Elevation** — glow, not offset: `0 0 24px -8px currentColor` accent halo; cards use a subtle inset top highlight + faint 1px drop. Avoid hard drop shadows.
**Motion** — fast 150ms · base 220ms · ease-out `cubic-bezier(0.16, 1, 0.3, 1)`. Reduced-motion: keep only the truthful live signals.

---

## Assets

- **No raster assets.** Brand identity = the coral OBSBOT ring glyph (inline SVG in the HTML) + the coral wordmark treatment. Reproduce the ring as a QML `Rectangle`/`Canvas` (2–3px coral ring + center dot + glow).
- Icons = **lucide** (2px stroke). Bundle lucide SVGs or use a Qt icon provider; match names in the rail/component list.
- Fonts = JetBrains Mono (bundle the family) + system sans.

---

## Files

- `OBSBOT Command Center.dc.html` — the interactive design reference (open in a browser to explore every state and interaction).
- `screenshots/01-…07-…png` — Control, Image, Tracking, Presets, Log, Disconnected, Discovering.

---

## Acceptance checklist (for Claude Code)

- [ ] Native Qt 6 / QML app, Wayland target; frameless/CSD window ~1180×748, resizable, 12px corners.
- [ ] Left icon rail with Control / Image / Track / Presets / Log; active item shows coral edge bar + tint; bottom LINK heartbeat reflects connection.
- [ ] Penelope visual language reproduced: obsidian base, glass panels (hairline borders, no hard shadows), single coral accent, mono chrome + sans prose, lucide 2px icons, status via dots/pills. No emoji.
- [ ] Exact tokens applied (colors, type, spacing, radii, motion) per Design Tokens.
- [ ] **Control page:** viewfinder pinned/always visible; only the right status+controls column scrolls.
- [ ] All five pages implemented with the specified controls and layouts.
- [ ] Four connection states rendered: connected, discovering (scan), disconnected (no device + Rescan), asleep (dim + Wake). **SIM control removed**; connection state comes from the SDK.
- [ ] PTZ **STEP mode is the default**: one interaction = one bounded, clamped move; no continuous motion.
- [ ] Velocity mode scaffolded but **disabled by default**, gated behind a flag, with stop-on-release, stop-on-window-blur, stop-on-error, and a timeout/deadman stop stubbed in.
- [ ] Video preview is a GStreamer/ffplay **placeholder** with external-preview affordance; **no fake embedded feed**; viewfinder region is a single surface ready to host a real `VideoOutput`/GStreamer sink with overlays layered on top.
- [ ] Settings + presets persist to `./config/obsbot-tiny3-control.json`; presets store name, pan, tilt, zoom, and FOV (if available); state restores on relaunch.
- [ ] Image / exposure / HDR / WB controls are **capability-gated** — disabled with a clear hint when SDK support is unverified.
- [ ] Every user action and connection event appends a colored Log line; log auto-scrolls.
- [ ] SDK actions wired per the mapping table, with exact SDK calls confirmed against the OBSBOT SDK (no assumed signatures shipped).
- [ ] Reduced-motion respected; only truthful live signals animate.
