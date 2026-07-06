#!/usr/bin/env bash
set -euo pipefail
export COPYFILE_DISABLE=1

APP_NAME="BJLEDAmbilight"
VERSION="$(cat VERSION.txt 2>/dev/null || echo 0.1.0)"
DIST_DIR="dist"
APP_BUNDLE="build/${APP_NAME}.app"
PKG_ROOT="build/pkg-root"
VOLUME_DIR="build/dmg-root"

clean_macos_metadata() {
  local target="$1"
  find "$target" \( -name '._*' -o -name '.DS_Store' \) -delete
  while IFS= read -r path; do
    xattr -c "$path" 2>/dev/null || true
    xattr -d com.apple.FinderInfo "$path" 2>/dev/null || true
    xattr -d com.apple.fileprovider.fpfs#P "$path" 2>/dev/null || true
    xattr -d com.apple.provenance "$path" 2>/dev/null || true
    xattr -d com.apple.quarantine "$path" 2>/dev/null || true
  done < <(find "$target" -print)
}

make SIGN_IDENTITY="${SIGN_IDENTITY:-BJ LED Ambilight Local}" app test

mkdir -p "$DIST_DIR"
rm -rf "$PKG_ROOT" "$VOLUME_DIR"

rm -f "${DIST_DIR}/${APP_NAME}-${VERSION}-macos.pkg"
if [[ "${BJ_LED_BUILD_PKG:-0}" == "1" ]]; then
  pkgbuild \
    --component "$APP_BUNDLE" \
    --identifier "local.bj-led.ambilight" \
    --version "$VERSION" \
    --install-location "/Applications" \
    "${DIST_DIR}/${APP_NAME}-${VERSION}-macos.pkg"
fi

mkdir -p "$VOLUME_DIR"
ditto --noextattr --noqtn "$APP_BUNDLE" "$VOLUME_DIR/${APP_NAME}.app"
ln -s /Applications "$VOLUME_DIR/Applications"
clean_macos_metadata "$VOLUME_DIR"
rm -f "${DIST_DIR}/${APP_NAME}-${VERSION}-macos.dmg"
hdiutil create \
  -volname "BJ LED Ambilight" \
  -srcfolder "$VOLUME_DIR" \
  -ov \
  -format UDZO \
  "${DIST_DIR}/${APP_NAME}-${VERSION}-macos.dmg" >/dev/null

echo "macOS dmg: ${DIST_DIR}/${APP_NAME}-${VERSION}-macos.dmg"
if [[ "${BJ_LED_BUILD_PKG:-0}" == "1" ]]; then
  echo "macOS pkg: ${DIST_DIR}/${APP_NAME}-${VERSION}-macos.pkg"
fi
