#!/usr/bin/env bash
# install-desktop-launcher.sh
# Installs desktop launcher and terminal shortcut 'locc' for Linux OBSBOT Control Center.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPIMAGE_PATH="${SCRIPT_DIR}/dist/OBSBOT4Linux-x86_64.AppImage"
ICON_SRC="${SCRIPT_DIR}/qt/packaging/icons/obsbot4linux-256.png"

APPS_DIR="${HOME}/.local/share/applications"
ICONS_DIR="${HOME}/.local/share/icons/hicolor/256x256/apps"
BIN_DIR="${HOME}/.local/bin"

mkdir -p "${APPS_DIR}"
mkdir -p "${ICONS_DIR}"
mkdir -p "${BIN_DIR}"

if [[ -f "${ICON_SRC}" ]]; then
    cp -f "${ICON_SRC}" "${ICONS_DIR}/obsbot4linux.png"
    cp -f "${ICON_SRC}" "${ICONS_DIR}/locc.png"
    echo "✔ Icon installed to ${ICONS_DIR}/locc.png"
fi

# Ensure AppImage is executable
if [[ -f "${APPIMAGE_PATH}" ]]; then
    chmod +x "${APPIMAGE_PATH}"
fi

# Create desktop entry
DESKTOP_FILE="${APPS_DIR}/locc.desktop"
cat > "${DESKTOP_FILE}" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=Linux OBSBOT Control Center (locc)
GenericName=OBSBOT Camera Control
Comment=Linux Control Center for OBSBOT cameras
Exec=${APPIMAGE_PATH}
Icon=locc
Terminal=false
Categories=AudioVideo;Video;Utility;
Keywords=obsbot;locc;webcam;ptz;camera;gimbal;
StartupWMClass=obsbot4linux
EOF

chmod +x "${DESKTOP_FILE}"
echo "✔ Desktop entry created: ${DESKTOP_FILE}"

# Create terminal CLI shortcut: ~/.local/bin/locc
ln -sf "${APPIMAGE_PATH}" "${BIN_DIR}/locc"
echo "✔ CLI shortcut created: ${BIN_DIR}/locc"

# Update desktop database if tool exists
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${APPS_DIR}" || true
fi

echo ""
echo "Installation complete!"
echo "  - Desktop menu: 'Linux OBSBOT Control Center (locc)'"
echo "  - Terminal: type 'locc' to start (make sure ~/.local/bin is in your PATH)"
