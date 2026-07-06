# Contributing

Thanks for helping improve BJ LED Ambilight.

This project is a small native desktop app, so changes should stay practical:
fast startup, low CPU use, low memory use, and no runtime dependency on heavy UI
frameworks.

## Ground Rules

- Keep source code under `src`.
- Keep public headers as `.hpp` files under `src/lib`.
- Do not add Qt, Electron, webviews, or background services.
- Keep platform code isolated in the platform folder that owns it.
- Prefer small, direct C++20 code over broad abstractions.
- Do not write screen frames or screenshots to disk during live capture.
- Keep release artifacts, generated builds, caches, and local screenshots out of git.

## Architecture

- Shared color logic belongs in `src/lib/bj_core.hpp`.
- macOS code belongs in `src/macos`.
- Windows code belongs in `src/platform/windows`.
- Linux code belongs in `src/platform/linux`.
- Each platform should use the same shared color pipeline where possible.
- Bluetooth backends should expose the smallest surface needed by the app.

## Style

- Use C++20.
- Keep files ASCII unless an existing file already uses another encoding.
- Use clear names and avoid comments that repeat the code.
- Prefer deterministic behavior over cleverness.
- Avoid allocations in hot capture/write paths when a fixed buffer or value type
  is enough.

## Tests

Run the default checks before pushing:

```bash
make test
make lint
```

Core logic has an enforced coverage target:

```bash
make coverage
```

The coverage target must stay at or above 99% line coverage for
`src/lib/bj_core.hpp`. Platform UI, OS capture, and Bluetooth code still need
manual validation on real hardware because those paths depend on OS permission
models and Bluetooth stacks.

## Platform Validation

For UI or Bluetooth changes, test the affected OS directly:

- macOS: screen capture permission, display selection, live Auto mode, manual
  colors, Bluetooth reconnect.
- Windows: installer, portable zip, scan, connect, live capture, dragging the
  window, slider interaction.
- Linux: X11 or XWayland launch, BlueZ scan/connect, slider interaction, log
  clipping.

## Commits

Use short lowercase two-word commit messages, for example:

```text
layout scan
version bump
readme polish
```

## Pull Requests

A good pull request includes:

- What changed.
- Which platform was tested.
- Which commands passed.
- Any hardware or OS limitation that could affect reviewers.

Keep PRs focused. Separate UI polish, Bluetooth behavior, capture behavior, and
release automation when possible.
