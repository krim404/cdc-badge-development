#!/usr/bin/env bash
# Build a self-contained Linux AppImage of the emulator (T053, FR-028).
# Bundles the SDL2 window frontend; linuxdeploy pulls SDL2 + its X11/Wayland
# client libs into the image. Run from the repo root on a Linux host:
#
#   emulator/packaging/build_appimage.sh [build-dir]
#
# Produces cdc-badge-emulator-x86_64.AppImage in the current directory.
set -euo pipefail

BUILD_DIR="${1:-emulator/build-appimage}"
APPDIR="$BUILD_DIR/AppDir"
ARCH="$(uname -m)"
LINUXDEPLOY="$BUILD_DIR/linuxdeploy-$ARCH.AppImage"

cmake -S emulator -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DEMULATOR_STATIC_RUNTIME=ON \
    -DEMULATOR_WINDOW=ON
cmake --build "$BUILD_DIR" --parallel

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$BUILD_DIR/cdc-badge-emulator" "$APPDIR/usr/bin/"

cat > "$APPDIR/usr/share/applications/cdc-badge-emulator.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=CDC Badge Emulator
Exec=cdc-badge-emulator
Icon=cdc-badge-emulator
Categories=Development;
Terminal=true
EOF

# Minimal placeholder icon (single dark pixel PNG) - the emulator is a CLI
# tool; the desktop entry only exists to satisfy the AppImage format.
printf '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\rIDATx\x9cc```\xf8\x0f\x00\x01\x04\x01\x00\x7f\xdb\x99\x9d\x00\x00\x00\x00IEND\xaeB`\x82' \
    > "$APPDIR/usr/share/icons/hicolor/256x256/apps/cdc-badge-emulator.png"

if [ ! -x "$LINUXDEPLOY" ]; then
    curl -fsSL -o "$LINUXDEPLOY" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
    chmod +x "$LINUXDEPLOY"
fi

"$LINUXDEPLOY" --appdir "$APPDIR" --output appimage

# appimagetool names the file after the desktop entry (CDC_Badge_Emulator-*);
# normalise to the artifact name the release pipeline expects.
PRODUCED=$(ls -1 ./*.AppImage | grep -v linuxdeploy | head -1)
mv "$PRODUCED" cdc-badge-emulator-x86_64.AppImage
echo "AppImage written to cdc-badge-emulator-x86_64.AppImage"
