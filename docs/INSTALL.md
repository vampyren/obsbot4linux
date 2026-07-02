# Installation & Build Guide

OBSBOT4Linux — native Linux (Qt 6 / QML). This covers the SDK
requirement, the prebuilt AppImage, building from source, and troubleshooting.

---

## 1. The OBSBOT SDK (required, not included)

This app links OBSBOT's proprietary **`libdev`** SDK. It is **not** distributed
with this repository — you must obtain it from OBSBOT and place it under `sdk/`:

```
sdk/
└── libdev_v2.1.0_8/
    ├── include/            # dev/dev.hpp, dev/devs.hpp, util/comm.hpp
    └── linux/
        └── x86_64-release/
            ├── libdev.so
            ├── libdev.so.1
            └── libdev.so.1.0.3
```

The build expects `sdk/libdev_v2.1.0_8/linux/x86_64-release/libdev.so`. If your
SDK version directory is named differently, either rename it to
`libdev_v2.1.0_8` or point the build at it:

```sh
cmake -S qt -B qt/build -DSDK_ROOT=/absolute/path/to/your/libdev_dir
```

The `sdk/` folder is git-ignored, so it never gets committed or published.

---

## 2. Prebuilt AppImage (easiest)

For modern 64-bit Linux with a recent glibc (rolling distros like Arch/CachyOS,
current Ubuntu/Fedora, etc.):

```sh
chmod +x OBSBOT4Linux-x86_64.AppImage
./OBSBOT4Linux-x86_64.AppImage

# confirm it sees your camera (headless, no window):
./OBSBOT4Linux-x86_64.AppImage --self-test
```

The AppImage bundles Qt, the QML runtime and the OBSBOT SDK; it uses your
system's GPU/OpenGL and X11/xcb libraries (so it runs on KDE and GNOME via
XWayland). Nothing is installed system-wide.

**FUSE note:** AppImages mount via FUSE2. If your distro only ships FUSE3 and you
get a mount error, either install FUSE2 (`sudo pacman -S fuse2` on Arch) or run:

```sh
./OBSBOT4Linux-x86_64.AppImage --appimage-extract-and-run
```

**GL fallback:** if a specific GPU/driver can't create an OpenGL context, force
software rendering:

```sh
QT_QUICK_BACKEND=software ./OBSBOT4Linux-x86_64.AppImage
```

---

## 3. Build from source

### Dependencies

**Arch / CachyOS**
```sh
sudo pacman -S --needed cmake qt6-base qt6-declarative qt6-wayland
# optional: ttf-jetbrains-mono (chrome font), ffmpeg (external preview)
```

**Ubuntu / Debian**
```sh
sudo apt install cmake g++ \
  qt6-base-dev qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-window \
  qt6-wayland
```

**Fedora**
```sh
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland
```

Requires **Qt 6.5+** and **CMake 3.21+**.

### Build & run

The repo-root launcher builds on first run and launches with the right library
paths and platform:

```sh
./obsbot4linux
```

or manually:

```sh
cmake -S qt -B qt/build -DCMAKE_BUILD_TYPE=Release
cmake --build qt/build -j
./qt/build/obsbot4linux
```

The binary is linked with an rpath to `sdk/.../libdev.so`, so there is no global
SDK install and no `sudo`.

### Hardware-free tests

```sh
ctest --test-dir qt/build --output-on-failure         # settings/preset persistence
./qt/build/obsbot4linux --self-test         # discovery + clean shutdown
QT_QPA_PLATFORM=offscreen ./qt/build/obsbot4linux   # load the full UI headless
```

### Build your own AppImage

```sh
qt/packaging/build-appimage.sh        # → dist/OBSBOT4Linux-x86_64.AppImage
```

On a normal desktop this needs no extra setup; it bundles Qt + the SDK and prunes
host GL/X11 libraries so the target's own drivers are used. See
`qt/packaging/README.md` for details and options (e.g. `WITH_WAYLAND=1`).

---

## 4. Running & permissions

- **No sudo needed.** SDK USB discovery works unprivileged; you just need to be in
  the `video` group (default on most distros): `groups | grep video`.
- **`ffplay` preview** is optional — install `ffmpeg` to enable the preview button.
  It opens the camera in a separate window and will conflict with a browser / OBS /
  Meet using the camera at the same time.
- **Config** is stored per-user at
  `~/.config/obsbot4linux/obsbot4linux.json`
  (override with `OBSBOT4LINUX_CONFIG=/path/to/file`).

---

## 5. Troubleshooting

| Symptom | Fix |
|---|---|
| `libdev.so not found` at configure | Place the SDK under `sdk/` (section 1) or pass `-DSDK_ROOT=…`. |
| Camera not detected (`--self-test` says NO DEVICE) | Check USB; ensure you're in the `video` group; close other apps holding the camera. |
| AppImage won't mount | Install FUSE2 or use `--appimage-extract-and-run`. |
| `QRhiGles2: Failed to create context` | `QT_QUICK_BACKEND=software ./…AppImage`. |
| Wants a specific platform | `QT_QPA_PLATFORM=xcb` (or `wayland`) before the binary. |
| Preview shows MJPEG warnings | Harmless camera-stream quirks; the app runs `ffplay -loglevel error` to hide them. |
