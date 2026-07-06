# BJ LED Ambilight

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
