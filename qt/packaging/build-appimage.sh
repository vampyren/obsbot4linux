#!/usr/bin/env bash
# Build a self-contained AppImage of the OBSBOT4Linux app.
#
# Targets MODERN Linux (recent glibc) — no old-distro compatibility shims. The
# resulting AppImage bundles Qt 6, the QML runtime, the platform plugins, and the
# OBSBOT SDK (libdev.so), so end users just download it, `chmod +x`, and run.
#
# Usage:
#   qt/packaging/build-appimage.sh
#
# Qt discovery: uses `qmake`/`qmake6` from PATH by default. To build against a
# non-PATH Qt (e.g. the sandbox aqt install), set QMAKE and CMAKE_PREFIX_PATH:
#   QMAKE=/path/to/Qt/6.x/gcc_64/bin/qmake \
#   CMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64 qt/packaging/build-appimage.sh
set -euo pipefail

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)   # qt/packaging
QT_DIR=$(cd -- "$HERE/.." && pwd)                   # qt/
REPO=$(cd -- "$QT_DIR/.." && pwd)                   # repo root
SDK_LIB="$REPO/sdk/libdev_v2.1.0_8/linux/x86_64-release"

BUILD="$QT_DIR/build-appimage"
APPDIR="$QT_DIR/AppDir"
TOOLS="$HERE/tools"
DIST="$REPO/dist"
OUT="$DIST/OBSBOT4Linux-x86_64.AppImage"
mkdir -p "$DIST"

mkdir -p "$TOOLS"
# Nested AppImage tools run without FUSE this way (robust in containers/VMs).
export APPIMAGE_EXTRACT_AND_RUN=1

# --- locate qmake -----------------------------------------------------------
QMAKE_BIN="${QMAKE:-}"
if [ -z "$QMAKE_BIN" ]; then
    QMAKE_BIN=$(command -v qmake6 || command -v qmake || true)
fi
if [ -z "$QMAKE_BIN" ]; then
    echo "error: qmake not found. Install Qt 6 dev packages, or set QMAKE=..." >&2
    exit 1
fi
QT_BIN_DIR=$(dirname "$QMAKE_BIN")
QT_LIB_DIR=$("$QMAKE_BIN" -query QT_INSTALL_LIBS 2>/dev/null || echo "$QT_BIN_DIR/../lib")
export PATH="$QT_BIN_DIR:$TOOLS:$PATH"   # so qmlimportscanner + tools are found
# linuxdeploy resolves ELF deps via LD_LIBRARY_PATH — Qt libs + the SDK must be here.
export LD_LIBRARY_PATH="$QT_LIB_DIR:$SDK_LIB:${LD_LIBRARY_PATH:-}"
echo ">> using qmake: $QMAKE_BIN"
echo ">> Qt libs:     $QT_LIB_DIR"

# --- fetch deploy tooling (modern linuxdeploy + qt plugin + appimagetool) ----
fetch() { # url dest
    [ -f "$2" ] && return 0
    echo ">> downloading $(basename "$2")"
    curl -fL --retry 3 -o "$2" "$1"
    chmod +x "$2"
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
      "$TOOLS/linuxdeploy-x86_64.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
      "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"
fetch "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
      "$TOOLS/appimagetool-x86_64.AppImage"
ln -sf appimagetool-x86_64.AppImage "$TOOLS/appimagetool"

# --- configure + build + install into AppDir --------------------------------
rm -rf "$BUILD" "$APPDIR"
cmake -S "$QT_DIR" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
cmake --build "$BUILD" -j
DESTDIR="$APPDIR" cmake --install "$BUILD" --prefix /usr

# --- bundle the OBSBOT SDK (.so + SONAME symlink) ---------------------------
mkdir -p "$APPDIR/usr/lib"
cp -av "$SDK_LIB/libdev.so.1.0.3" "$APPDIR/usr/lib/"
ln -sf libdev.so.1.0.3 "$APPDIR/usr/lib/libdev.so.1"

# --- Qt plugin config: scan our QML so the right modules are bundled --------
export QML_SOURCES_PATHS="$QT_DIR/qml"
export QMAKE="$QMAKE_BIN"
# xcb is always deployed by the qt plugin (runs on KDE + GNOME via X11/XWayland).
# Also bundle the offscreen plugin so `--self-test` works headlessly. Native
# Wayland is opt-in (WITH_WAYLAND=1) — it needs the wayland client libs +
# integration plugins present on the build machine.
EXTRA="libqoffscreen.so"
if [ "${WITH_WAYLAND:-0}" = "1" ] && ls "$QT_LIB_DIR/../plugins/platforms/"libqwayland*.so >/dev/null 2>&1; then
    EXTRA="$EXTRA;libqwayland-generic.so;libqwayland-egl.so"
    echo ">> native Wayland platform requested"
fi
export EXTRA_PLATFORM_PLUGINS="$EXTRA"

# --- deploy dependencies into the AppDir (no AppImage yet) -------------------
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --plugin qt

# The embedded preview decodes the camera's MJPG frames with QImage, which
# needs Qt's JPEG imageformats plugin at runtime — linuxdeploy-plugin-qt does
# not bundle it for a QML app (it only follows declared QML/module deps), and
# without it the preview decodes ZERO frames inside the AppImage.
QT_PLUGIN_DIR=$("$QMAKE_BIN" -query QT_INSTALL_PLUGINS)
if [ -f "$QT_PLUGIN_DIR/imageformats/libqjpeg.so" ]; then
    mkdir -p "$APPDIR/usr/plugins/imageformats"
    cp -f "$QT_PLUGIN_DIR/imageformats/libqjpeg.so" "$APPDIR/usr/plugins/imageformats/"
    echo ">> bundled imageformats/libqjpeg.so (MJPG preview decode)"
else
    echo "error: libqjpeg.so not found under $QT_PLUGIN_DIR — preview would not decode" >&2
    exit 1
fi

# CRITICAL: prune host-provided system libraries so the TARGET distro's own,
# ABI-matching copies are used. Bundling the GPU stack breaks GL context
# creation; bundling the X11/xcb/xkbcommon stack (especially when the build host
# and target are different distros) causes hard crashes on startup. This is the
# canonical AppImage "excludelist" — the community-vetted set of libs that are
# always present on a desktop target and must NOT be shipped.
echo ">> pruning host-provided libs per the AppImage excludelist"
EXCL=$(curl -fsSL "https://raw.githubusercontent.com/AppImage/pkg2appimage/master/excludelist" 2>/dev/null \
        | sed 's/#.*//' | tr -d '[:blank:]' | grep -v '^$' || true)
if [ -z "$EXCL" ]; then
    # Fallback list if the excludelist can't be fetched (offline).
    EXCL="libGL.so.1 libGLX.so.0 libGLdispatch.so.0 libEGL.so.1 libgbm.so.1 libdrm.so.2
          libglapi.so.0 libwayland-egl.so.1 libX11.so.6 libX11-xcb.so.1 libxcb.so.1
          libxcb-cursor.so.0 libxcb-glx.so.0 libxcb-render.so.0 libxcb-shm.so.0
          libxcb-icccm.so.4 libxcb-image.so.0 libxcb-keysyms.so.1 libxcb-randr.so.0
          libxcb-render-util.so.0 libxcb-shape.so.0 libxcb-sync.so.1 libxcb-util.so.1
          libxcb-xfixes.so.0 libxcb-xinerama.so.0 libxcb-xinput.so.0 libxcb-xkb.so.1
          libXau.so.6 libXdmcp.so.6 libxkbcommon.so.0 libxkbcommon-x11.so.0
          libSM.so.6 libICE.so.6 libfontconfig.so.1 libexpat.so.1"
fi
for lib in $EXCL; do rm -f "$APPDIR"/usr/lib/"$lib"* 2>/dev/null || true; done

# Belt-and-braces: force-remove the entire display-server client + GPU stack.
# Some (e.g. libxcb-cursor) are newer than the upstream excludelist; all are
# guaranteed present on any Linux desktop and MUST come from the host to match
# its X server / GPU driver.
rm -f "$APPDIR"/usr/lib/libxcb*.so*        "$APPDIR"/usr/lib/libX11*.so* \
      "$APPDIR"/usr/lib/libxkbcommon*.so*  "$APPDIR"/usr/lib/libXau*.so* \
      "$APPDIR"/usr/lib/libXdmcp*.so*      "$APPDIR"/usr/lib/libSM*.so* \
      "$APPDIR"/usr/lib/libICE*.so*        "$APPDIR"/usr/lib/libwayland*.so* \
      "$APPDIR"/usr/lib/libxshmfence*.so*  "$APPDIR"/usr/lib/libGL*.so* \
      "$APPDIR"/usr/lib/libEGL*.so*        "$APPDIR"/usr/lib/libgbm*.so* \
      "$APPDIR"/usr/lib/libdrm*.so*        "$APPDIR"/usr/lib/libglapi*.so* 2>/dev/null || true

# NOTE: platform steering (force xcb under a wayland session) is done inside the
# app binary, gated on $APPDIR/$APPIMAGE — see main.cpp. No AppRun hook needed.

# --- build the AppImage from the pruned AppDir ------------------------------
rm -f "$OUT"
ARCH=x86_64 "$TOOLS/appimagetool" "$APPDIR" "$OUT"
chmod +x "$OUT"
echo ">> done: $OUT"
