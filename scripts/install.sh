#!/usr/bin/env bash
set -euo pipefail

VERSION="${BJ_LED_VERSION:-0.1.12}"
REPOSITORY="${BJ_LED_REPOSITORY:-alman790/bj-led-screen-sync}"
BASE_URL="${BJ_LED_RELEASE_BASE_URL:-https://github.com/${REPOSITORY}/releases/download/v${VERSION}/}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

download() {
  local name="$1"
  local url="${BASE_URL}${name}"
  echo "Downloading $url"
  curl -fL --progress-bar "$url" -o "$TMP_DIR/$name"
}

install_macos() {
  local name="BJLEDAmbilight-${VERSION}-macos.dmg"
  download "$name"
  local mount_dir="$TMP_DIR/mount"
  mkdir -p "$mount_dir" "$HOME/Applications"
  hdiutil attach "$TMP_DIR/$name" -mountpoint "$mount_dir" -nobrowse -readonly >/dev/null
  rm -rf "$HOME/Applications/BJLEDAmbilight.app"
  ditto "$mount_dir/BJLEDAmbilight.app" "$HOME/Applications/BJLEDAmbilight.app"
  xattr -cr "$HOME/Applications/BJLEDAmbilight.app" 2>/dev/null || true
  hdiutil detach "$mount_dir" >/dev/null
  echo "Installed: $HOME/Applications/BJLEDAmbilight.app"
}

install_linux() {
  if command -v dpkg >/dev/null 2>&1; then
    local name="bj-led-ambilight_${VERSION}_amd64.deb"
    download "$name"
    sudo dpkg -i "$TMP_DIR/$name" || sudo apt-get install -f -y
    echo "Installed: /usr/bin/bj-led-ambilight"
  else
    local name="bj-led-ambilight-${VERSION}-linux-amd64.tar.gz"
    download "$name"
    mkdir -p "$HOME/.local"
    tar -C "$HOME/.local" -xzf "$TMP_DIR/$name"
    echo "Installed into: $HOME/.local"
  fi
}

case "$(uname -s)" in
  Darwin) install_macos ;;
  Linux) install_linux ;;
  *) echo "Unsupported OS. Use scripts/install-windows.ps1 on Windows."; exit 1 ;;
esac
