<p align="center">
  <img src="src/resources/icons/app-icon-256.png" alt="BJ LED Ambilight" width="128" height="128">
</p>

<h1 align="center">BJ LED Ambilight</h1>

<p align="center">
  Native screen-reactive lighting for BJ_LED Bluetooth strips.
</p>

<p align="center">
  <a href="https://github.com/alman790/bj-led-screen-sync/actions/workflows/ci.yml"><img alt="CI" src="https://img.shields.io/github/actions/workflow/status/alman790/bj-led-screen-sync/ci.yml?branch=main&label=ci&style=for-the-badge"></a>
  <a href="https://github.com/alman790/bj-led-screen-sync/releases"><img alt="Release" src="https://img.shields.io/github/v/release/alman790/bj-led-screen-sync?include_prereleases&label=release&style=for-the-badge"></a>
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white&style=for-the-badge">
  <img alt="License" src="https://img.shields.io/github/license/alman790/bj-led-screen-sync?style=for-the-badge">
</p>

<p align="center">
  <img alt="macOS" src="https://img.shields.io/badge/macOS-primary-111111?logo=apple&logoColor=white&style=flat-square">
  <img alt="Linux" src="https://img.shields.io/badge/Linux-experimental-FCC624?logo=linux&logoColor=111111&style=flat-square">
  <img alt="Windows" src="https://img.shields.io/badge/Windows-experimental-0078D4?logo=windows&logoColor=white&style=flat-square">
  <img alt="Bluetooth LE" src="https://img.shields.io/badge/Bluetooth-LE-0082FC?logo=bluetooth&logoColor=white&style=flat-square">
</p>

BJ LED Ambilight is a native desktop app that syncs a `BJ_LED` / `BJ_LED_M`
Bluetooth LED strip with the colors on your screen.

It is built for a lightweight, always-on ambilight setup: the app captures a
downscaled frame, analyzes the screen edges and corners, and sends compact RGB
updates to the LED strip over Bluetooth.

## Highlights

- Native macOS app with a translucent control panel.
- Cross-platform C++ core for color analysis and BJ_LED packet generation.
- Zoned screen analysis using edges and corners instead of a single flat average.
- Auto output mode for screen-reactive lighting.
- Manual output modes for fixed red, green, blue, and white colors.
- Low-resolution in-memory sampling designed to keep CPU and memory usage small.
- No screenshot files are written during live capture.

## Platform Support

| Platform | Status | Capture | Bluetooth |
| --- | --- | --- | --- |
| macOS | Primary | CoreGraphics composited display capture | CoreBluetooth |
| Linux | Experimental | X11 / XWayland | BlueZ |
| Windows | Experimental | Win32 / GDI | Windows Bluetooth GATT |

The shared color pipeline is implemented in C++ and reused across platforms.
Platform-specific code is isolated under `src/platform` and `src/macos`.

## Security Note

Windows antivirus tools can warn about new unsigned installers or portable
builds, especially before a release has reputation history. The project does
not include malware: the source is public, release artifacts are built by
GitHub Actions, and checksums are published with each release.

## LED Protocol

The current BJ_LED protocol support writes one RGB color to the strip:

```text
Characteristic: 0000ee01-0000-1000-8000-00805f9b34fb
Packet:         69 96 05 02 RR GG BB WW
```

The app already computes separate edge and corner colors internally. Multi-zone
output will require a confirmed segmented or addressable BJ_LED protocol.

## Project Layout

```text
src/lib/bj_core.hpp                  shared RGB type, color analysis, packet format
src/macos/                           macOS AppKit, capture, and Bluetooth backend
src/platform/linux/                  Linux capture and BlueZ backend
src/platform/windows/                Windows UI, capture, and Bluetooth backend
src/resources/                       app icons and macOS bundle resources
```

## Resource Model

- Pixel storage uses `union bj::RGB`.
- `sizeof(bj::RGB) == 4` is enforced at compile time.
- Default sample buffer: `160 x 90 x 4`, about 57 KB.
- Bluetooth writes use fixed 8-byte packets.
- Live capture uses memory buffers only.

## License

MIT. See [LICENSE](LICENSE).
