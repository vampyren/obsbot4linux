# OBSBOT Tiny 3 — Linux SDK Integration Spike Report

**Goal:** Build the first Linux SDK integration spike for OBSBOT Tiny 3 control
(a minimal probe), before building the full GUI.

**Status:** ✅ Probe builds and runs. SDK links against the Linux x86_64
`libdev.so` and executes device discovery. Tiny 3 enum support is confirmed in
the SDK headers and wired into the probe. **No physical device was connected in
this environment**, so the probe correctly reported "no device found" and exited
non-zero — no success was faked.

---

## 1. Files changed

All new; the repo previously contained only `libdev_v2_1_0_8.zip`.

| File | Purpose |
|------|---------|
| `sdk-probe/main.cpp` | The probe: SDK discovery + device info + safe command path |
| `sdk-probe/Makefile` | Primary build (no cmake dependency), `$ORIGIN` rpath |
| `sdk-probe/CMakeLists.txt` | Optional Linux-correct CMake build (alt to Makefile) |
| `sdk-probe/README.md` | Developer note: SDK path, lib, Tiny 3 enum, runtime loading, limitations, next step |
| `.gitignore` | Excludes build outputs, macOS cruft, and the 66 MB SDK zip |
| `SDK_PROBE_REPORT.md` | This report |

The extracted SDK tree `libdev_v2.1.0_8/` (from the zip) is left in place and
referenced by the build; it was not modified.

---

## 2. Repo layout found

```
obsbot/
├── libdev_v2_1_0_8.zip            # original SDK archive (~66 MB)
├── libdev_v2.1.0_8/              # extracted SDK
│   ├── include/dev/{dev.hpp,devs.hpp}
│   ├── include/util/comm.hpp
│   ├── linux/x86_64-release/libdev.so(.1)(.1.0.3)   # SONAME libdev.so.1.0.3
│   ├── linux/arm64-release/libdev.so...
│   ├── macos/... windows/...
│   └── OBSBOT_Sample/{main.cpp,CMakeLists.txt}
└── sdk-probe/                    # NEW — this spike
```

---

## 3. How to build

Primary (Makefile, no cmake required — cmake is not installed on this box):

```bash
cd sdk-probe
make
```

Observed command line:

```
g++ -std=c++17 -O2 -Wall -Wextra -I../libdev_v2.1.0_8/include main.cpp \
    -o obsbot-sdk-probe -L../libdev_v2.1.0_8/linux/x86_64-release \
    '-Wl,-rpath,$ORIGIN/../libdev_v2.1.0_8/linux/x86_64-release' -ldev -lpthread
```

Optional CMake alternative (Linux-correct — see §7):

```bash
cd sdk-probe && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Toolchain used: `g++ (Ubuntu 15.2.0)`. SDK `SONAME = libdev.so.1.0.3`;
`ldd`/`readelf` confirm the binary resolves it via `$ORIGIN` RUNPATH with no
global install.

---

## 4. How to run the probe

```bash
cd sdk-probe
./obsbot-sdk-probe             # discover + list (read-only, default)
./obsbot-sdk-probe --status   # + live zoom / AI-mode snapshot
./obsbot-sdk-probe --wake     # run state -> Run
./obsbot-sdk-probe --sleep    # run state -> Sleep
./obsbot-sdk-probe --center   # gimbal reset to zero (single, bounded)
./obsbot-sdk-probe --sn <SN>  # target a specific device
./obsbot-sdk-probe --wait-ms 6000   # longer USB settle time
```

Exit `0` = ≥1 OBSBOT device found; non-zero otherwise.

---

## 5. Actual probe output

`lsusb` in this environment showed **only USB root hubs — no OBSBOT device
connected.** Running the probe:

```
obsbot-sdk-probe: SDK lib version 1.3.0
d-d: token = aa d8 1d a0 35 81
d-i: start the device detection thread in linux
d-i: start the device bluetooth thread
d-d: Local IPv4 address: 192.168.1.60
d-d: Open 3 sockets for mDNS query
Waiting 3000 ms for USB discovery ...

ERROR: no OBSBOT device found.
Diagnostics:
  - Is the Tiny 3 connected via USB and powered?
  - Try a longer settle time: --wait-ms 6000
  - Check `lsusb` for an OBSBOT / Remo entry.
  - USB control may need udev permissions; try running
    with sufficient privileges or add a udev rule.
d-i: start to destroy devices object
d-i: exit the device detection thread
d-i: destroy devices object successfully
```

Exit code: **1** (correct — no device, and not faked).

**What this proves:** the SDK loads (`version 1.3.0`), the Linux detection
thread starts (`start the device detection thread in linux`), USB discovery
runs, the no-device error path works, and the SDK shuts down cleanly via
`Devices::close()`. Arg parsing (`--help`, unknown arg, `--wake --sleep`
conflict) was also verified and returns the expected codes (0 / 2 / 2).

**To validate with hardware:** connect the Tiny 3 over USB and re-run
`./obsbot-sdk-probe`. Expected per-device block:

```
  name:         <device name>
  SN:           <14-char serial>
  version:      <firmware, e.g. 1.2.3.4>
  product:      Tiny3 (enum 18)
  mode:         UVC
  is Tiny 3:    YES
...
Tiny 3 detected: YES
```

---

## 6. Whether Tiny 3 was detected

- **In the SDK headers: YES.** `include/dev/dev.hpp` defines
  `ObsbotProdTiny3 = 18` and `ObsbotProdTiny3Lite = 19`. Both are mapped in the
  probe, and `productType() == ObsbotProdTiny3` drives the `is Tiny 3` flag.
- **At runtime: not observable here** — no device was connected. The probe is
  built to recognize and clearly report Tiny 3 the moment the SDK enumerates one.

---

## 7. Known limitations

- **SDK control ≠ browser/WebRTC resolution selection.** This SDK drives PTZ /
  AI / power / gimbal over the USB *control* channel. It does not choose the
  resolution a browser negotiates for the UVC video stream (that is picked by
  the browser/UVC stack independently).
- **No hardware in this environment** — the with-device path is code-complete
  but unverified against a real Tiny 3.
- **USB permissions** — discovery of a connected device may require a udev rule
  or elevated privileges.
- **mDNS sockets still open** — even with `setEnableMdnsScan(false)` the SDK
  opens mDNS sockets at init; it just does not actively use network discovery.
- **`--status` for Tiny 3** reads the `tiny` status view (zoom / AI mode) as a
  best-effort snapshot; some fields may differ across the tiny sub-models.

**Next step after probe:** a **Wayland-native GUI** with controls for PTZ,
presets, tracking, sleep/run, zoom, FOV, and status — on top of this validated
SDK integration.

---

## 8. Self code review

Reviewed the diff against the required checklist; findings and resolutions:

| Check | Result |
|-------|--------|
| Wrong Linux vs Windows SDK paths | **Fixed by design.** Build points at `linux/x86_64-release`. The upstream sample's `else()` branch (Windows paths + MSVC `libdev`) was **not** copied; the optional CMake adds a real Linux branch instead. |
| Broken rpath / runtime loading | **Verified.** SONAME is `libdev.so.1.0.3`; `readelf -d` shows `RUNPATH=$ORIGIN/../libdev_v2.1.0_8/linux/x86_64-release` and `ldd` resolves the lib with no global install. `make run` / `LD_LIBRARY_PATH` documented as fallback. |
| Accidentally committed build outputs | **Prevented.** `.gitignore` excludes `sdk-probe/obsbot-sdk-probe`, `build/`, `*.o`, `__MACOSX/`, `.DS_Store`, and the 66 MB zip. (Repo is not yet a git repo, so nothing is actually committed.) |
| Unsafe camera movement | **Safe.** Only run/sleep state, a read-only status snapshot, and a single bounded `gimbalRstPosR()` reset. No movement loops, speed bursts, or angle sweeps. `--wake`/`--sleep` are mutually exclusive. |
| Missing error handling (no device) | **Handled.** Empty device list → diagnostic message + exit 1. `--sn` not found → exit 1. Bad args → exit 2. |
| Missing documentation | **Added.** `README.md` (developer note) + this report cover SDK path, library, Tiny 3 enum, runtime loading, safety, limitations, and next step. |
| Test / build gaps | **Closed where possible.** Clean build with `-Wall -Wextra` produced **no warnings**; no-device run, `--help`, unknown-arg, and conflict paths all verified. With-device path cannot be exercised without hardware (documented). |

No meaningful issues remained open after review.
