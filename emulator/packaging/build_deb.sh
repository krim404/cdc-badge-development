#!/usr/bin/env bash
# Build a .deb of the emulator (T053 optional artifact). Packages the
# statically-linked headless-capable binary plus the window frontend when SDL2
# was available at build time (SDL2 then becomes a package dependency).
#
#   emulator/packaging/build_deb.sh [build-dir] [version]
#
# Produces cdc-badge-emulator_<version>_amd64.deb in the current directory.
set -euo pipefail

BUILD_DIR="${1:-emulator/build-deb}"
VERSION="${2:-0.1.0}"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
PKG_DIR="$BUILD_DIR/pkg"

cmake -S emulator -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DEMULATOR_STATIC_RUNTIME=ON
cmake --build "$BUILD_DIR" --parallel

# SDL2 is the only dynamic third-party dependency the binary may carry.
DEPENDS=""
if ldd "$BUILD_DIR/cdc-badge-emulator" 2>/dev/null | grep -q libSDL2; then
    DEPENDS="Depends: libsdl2-2.0-0"
fi

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/DEBIAN" "$PKG_DIR/usr/bin" "$PKG_DIR/usr/share/doc/cdc-badge-emulator"
cp "$BUILD_DIR/cdc-badge-emulator" "$PKG_DIR/usr/bin/"
cp emulator/README.md "$PKG_DIR/usr/share/doc/cdc-badge-emulator/"

cat > "$PKG_DIR/DEBIAN/control" <<EOF
Package: cdc-badge-emulator
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
$DEPENDS
Maintainer: CDC Badge Development <noreply@krim.dev>
Description: Off-device CDC Badge plugin emulator
 Runs CDC Badge WASM plugins on the desktop against the firmware's real
 drawing stack and host API. Development tool; the software secure element
 stores keys unencrypted.
EOF
# Empty Depends line breaks dpkg; drop it when static.
sed -i '/^$/d' "$PKG_DIR/DEBIAN/control"

dpkg-deb --build --root-owner-group "$PKG_DIR" \
    "cdc-badge-emulator_${VERSION}_${ARCH}.deb"
echo "wrote cdc-badge-emulator_${VERSION}_${ARCH}.deb"
