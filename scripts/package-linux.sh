#!/usr/bin/env bash
set -euo pipefail

APP_NAME="bj-led-ambilight"
VERSION="$(cat VERSION.txt 2>/dev/null || echo 0.1.0)"
DIST_DIR="dist"
BINARY="build/linux/${APP_NAME}-linux"
STAGE="build/linux-package/${APP_NAME}_${VERSION}_amd64"

make -f Makefile.linux

mkdir -p "$DIST_DIR"
rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/bin" "$STAGE/usr/share/applications" "$STAGE/usr/share/doc/${APP_NAME}" "$STAGE/usr/share/icons/hicolor/256x256/apps"

install -m 0755 "$BINARY" "$STAGE/usr/bin/${APP_NAME}"
install -m 0644 README.md "$STAGE/usr/share/doc/${APP_NAME}/README.md"
install -m 0644 src/resources/icons/app-icon-256.png "$STAGE/usr/share/icons/hicolor/256x256/apps/${APP_NAME}.png"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: ${APP_NAME}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: amd64
Depends: libc6, libstdc++6, libx11-6, bluez
Maintainer: BJ LED Ambilight
Description: Native BJ_LED ambilight controller
 Screen-color ambilight controller for BJ_LED/BJ_LED_M Bluetooth LED strips.
EOF

cat > "$STAGE/usr/share/applications/${APP_NAME}.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=BJ LED Ambilight
Exec=/usr/bin/${APP_NAME}
Icon=${APP_NAME}
Terminal=true
Categories=Utility;
EOF

if command -v dpkg-deb >/dev/null 2>&1; then
  dpkg-deb --build "$STAGE" "${DIST_DIR}/${APP_NAME}_${VERSION}_amd64.deb"
  echo "Linux deb: ${DIST_DIR}/${APP_NAME}_${VERSION}_amd64.deb"
else
  echo "dpkg-deb not found; skipped .deb"
fi

tar -C "$STAGE" -czf "${DIST_DIR}/${APP_NAME}-${VERSION}-linux-amd64.tar.gz" .
echo "Linux tar.gz: ${DIST_DIR}/${APP_NAME}-${VERSION}-linux-amd64.tar.gz"
