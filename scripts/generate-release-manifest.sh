#!/usr/bin/env bash
set -euo pipefail

VERSION="$(cat VERSION.txt 2>/dev/null || echo 0.1.0)"
REPOSITORY="${BJ_LED_REPOSITORY:-alman790/bj-led-screen-sync}"
BASE_URL="${BJ_LED_RELEASE_BASE_URL:-https://github.com/${REPOSITORY}/releases/download/v${VERSION}/}"
OUT="releases/manifest.json"

sha_for() {
  local file="$1"
  if [[ -f "dist/$file" ]]; then
    shasum -a 256 "dist/$file" | awk '{print $1}'
  fi
}

mkdir -p releases
cat > "$OUT" <<EOF
{
  "name": "BJ LED Ambilight",
  "latest": "${VERSION}",
  "repository": "${REPOSITORY}",
  "versions": [
    {
      "version": "${VERSION}",
      "tag": "v${VERSION}",
      "base_url": "${BASE_URL}",
      "files": [
        {
          "platform": "macos",
          "type": "dmg",
          "name": "BJLEDAmbilight-${VERSION}-macos.dmg",
          "sha256": "$(sha_for "BJLEDAmbilight-${VERSION}-macos.dmg")"
        },
        {
          "platform": "macos",
          "type": "zip",
          "name": "BJLEDAmbilight-${VERSION}-macos.zip",
          "sha256": "$(sha_for "BJLEDAmbilight-${VERSION}-macos.zip")"
        },
        {
          "platform": "linux",
          "type": "deb",
          "name": "bj-led-ambilight_${VERSION}_amd64.deb",
          "sha256": "$(sha_for "bj-led-ambilight_${VERSION}_amd64.deb")"
        },
        {
          "platform": "linux",
          "type": "tar.gz",
          "name": "bj-led-ambilight-${VERSION}-linux-amd64.tar.gz",
          "sha256": "$(sha_for "bj-led-ambilight-${VERSION}-linux-amd64.tar.gz")"
        },
        {
          "platform": "windows",
          "type": "setup.exe",
          "name": "BJLEDAmbilight-${VERSION}-windows-x64-setup.exe",
          "sha256": "$(sha_for "BJLEDAmbilight-${VERSION}-windows-x64-setup.exe")"
        },
        {
          "platform": "windows",
          "type": "zip",
          "name": "BJLEDAmbilight-${VERSION}-windows-x64.zip",
          "sha256": "$(sha_for "BJLEDAmbilight-${VERSION}-windows-x64.zip")"
        },
        {
          "platform": "source",
          "type": "zip",
          "name": "BJLEDAmbilight-${VERSION}-source.zip",
          "sha256": "$(sha_for "BJLEDAmbilight-${VERSION}-source.zip")"
        }
      ]
    }
  ]
}
EOF

echo "Wrote $OUT"
