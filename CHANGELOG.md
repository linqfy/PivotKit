# Changelog

User-visible changes are listed here. Releases are prereleases while PivotKit
remains experimental. The version in a `v*` Git tag must match a heading below.

## [Unreleased]

### Changed

- Made mod loading deterministic by sorting filenames before execution.
- Moved generated RTTI output under `docs/reference/` and clarified the
  beginner, build, architecture, and release guides.
- CI now runs both Lua suites and checks that release tags match the changelog.

## [0.4.0] — 2026-08-15

### Added

- A BepInEx-style console, enabled with `-console`, the
  `PIVOTKIT_CONSOLE` environment variable, or `pivot.console(true)` /
  `pivotlib.console()`.
- A loopback TCP bridge on `127.0.0.1:50077`, enabled by default, with the
  `tools/pivotctl.py` client.
- Polling-based mouse and keyboard events through `pivotlib`.
- Scene pinning and raw memory inspection through `pivot.peek`, `pivotlib.pin`,
  and `pivotlib.dump`.
- Best-effort batch/undo support and figure-builder automation.
- `mods/03_pivotlib_events.lua`, a click counter with HUD and bridge commands.

### Changed

- Bridge requests are processed on Pivot's main thread.
- `find_main_hwnd` caches the window handle for input and overlay work.
- The test mock covers the console, bridge, input polling, hooks, and memory
  inspection paths.

### Fixed

- Removed inline hooks on FMX mouse and keyboard handlers after intermittent
  access violations.
- Protected hook callbacks with SEH.

## [0.3.0] — hooks v2, overlay, mod manager, scene discovery

- Hook callbacks receive up to six arguments, convert Delphi strings to Lua
  strings, and preserve the original method's `ret N` stack cleanup.
- Added string detection, instance discovery, class enumeration, mod reload,
  and canvas overlay APIs.
- Added `pivotlib` hook management, commands, keybindings, scene discovery,
  SVG export, and HUD helpers.
- Added `mods/02_pivotlib_hud.lua`.
- Added RTTI tooling for all published classes and guarded inherited field-table
  traversal and class enumeration.

## [0.2.0] — abstraction layer

- Added the low-level pointer, reflection, typed-field, and string-aware APIs.
- Added `mods/00_pivotlib.lua`, the high-level proxy and semantic helper layer.
- Added the first demo mod, catalog tooling, mock API, and standalone Lua tests.
- Added the getting-started, API, build, and abstraction-layer guides.

## [0.1.0] — initial beta

- Injected a 32-bit Lua 5.4.8 runtime into Pivot Animator 5.2.11.
- Added the initial `pivot` API for logging, calls, fields, update callbacks,
  memory access, reflection, hooks, keyboard input, and sleep.
- Added GitHub Actions builds and tag-triggered prereleases.
