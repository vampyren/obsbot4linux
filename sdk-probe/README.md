# obsbot-sdk-probe — Linux SDK integration spike (developer note)

A minimal Linux/x86_64 probe that proves the official **OBSBOT device SDK**
(`libdev v2.1.0_8`) links and runs, discovers a USB-connected camera, reports
its identity, and can issue a few **safe** control commands. This is a spike
to de-risk the SDK before building the real GUI — it is **not** the GUI.

## SDK path and library used

| Item | Value |
|------|-------|
| SDK package | `libdev_v2.1.0_8` (extracted at repo root: `../libdev_v2.1.0_8`) |
| Headers | `../libdev_v2.1.0_8/include/dev/{dev.hpp,devs.hpp}`, `.../include/util/comm.hpp` |
| Linux x86_64 library | `../libdev_v2.1.0_8/linux/x86_64-release/libdev.so` (SONAME `libdev.so.1.0.3`) |
| Reported lib version | `get_dll_ver()` → `1.3.0` |

The **arm64** Linux build also ships (`.../linux/arm64-release/libdev.so`); the
CMake build auto-selects it on aarch64. The Makefile targets x86_64.

## Confirmed Tiny 3 enum support

`include/dev/dev.hpp` defines the product enum with explicit values:

```cpp
enum ObsbotProductType {
    ...
    ObsbotProdTiny3     = 18,
    ObsbotProdTiny3Lite = 19,
    ObsbotProdButt,
};
```

The probe maps both to human-readable names and flags `is Tiny 3` when
`productType() == ObsbotProdTiny3`.

## Build

Primary build is a plain Makefile (no cmake dependency):

```bash
cd sdk-probe
make
```

Optional CMake build (Linux-correct; does **not** reuse the SDK sample's
Windows-oriented CMake):

```bash
cd sdk-probe
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

```bash
cd sdk-probe
./obsbot-sdk-probe            # discover + list (read-only)
./obsbot-sdk-probe --status  # + live zoom / AI-mode snapshot
./obsbot-sdk-probe --wake    # set run state -> Run
./obsbot-sdk-probe --sleep   # set run state -> Sleep
./obsbot-sdk-probe --center  # reset gimbal to zero (single, bounded)
./obsbot-sdk-probe --sn <14-char-SN>   # target a specific device
./obsbot-sdk-probe --wait-ms 6000      # longer USB settle time
```

Exit code `0` = at least one OBSBOT device found; non-zero otherwise.
Commands act on `--sn` if given, else the first discovered device.

## Runtime library loading (how `libdev.so` is found)

The SDK's `SONAME` is `libdev.so.1.0.3`, so the loader looks for exactly that
name at runtime. The build embeds an **`$ORIGIN`-relative rpath** so the binary
finds the SDK `.so` next to the source tree — **no global install, no
`ldconfig`, no copy into `/usr/local`**:

```
RUNPATH = $ORIGIN/../libdev_v2.1.0_8/linux/x86_64-release
```

`$ORIGIN` resolves to the executable's own directory at load time. Verify with:

```bash
readelf -d obsbot-sdk-probe | grep -E 'RUNPATH|NEEDED'
ldd obsbot-sdk-probe | grep dev
```

If you move the binary away from `sdk-probe/`, the `$ORIGIN` rpath breaks. Use
the fallback:

```bash
make run   # sets LD_LIBRARY_PATH to the SDK lib dir
# or
LD_LIBRARY_PATH=../libdev_v2.1.0_8/linux/x86_64-release ./obsbot-sdk-probe
```

## Safety notes

Only low-risk commands are wired up: run/sleep state, a status read, and a
single gimbal **reset to zero** (`gimbalRstPosR()`). There are **no** movement
loops, speed-control bursts, or angle sweeps. `--wake` and `--sleep` are
mutually exclusive.

## Known limitations

- **SDK control is separate from the browser/WebRTC video path.** This SDK
  drives PTZ / AI / power / gimbal over the USB control channel. It does **not**
  select the resolution a browser (WebRTC/`getUserMedia`) negotiates for the
  UVC video stream — that is chosen by the browser/UVC stack independently.
- USB control may require udev permissions; if discovery fails despite a
  connected device, a udev rule (or elevated privileges) may be needed.
- Even with `setEnableMdnsScan(false)`, the SDK still opens mDNS sockets during
  init (visible in its logs); it just does not actively use network discovery.
- The SDK emits its own `d-i` / `d-d` log lines to the console; these are the
  library's diagnostics, not the probe's output.

## Next step after this probe

Build a **Wayland-native GUI** with controls for: PTZ, presets, tracking,
sleep/run, zoom, FOV, and status readout — layered on top of this validated SDK
integration.
