<!-- Logo source: docs/assets/pivotkit-logo.svg -->
<div align="center">

  <p><img src="docs/assets/pivotkit-logo.svg" alt="PivotKit logo" width="520"></p>

  <h1>PivotKit</h1>

  <p>Lua mods for Pivot Animator 5.2.11.</p>

  <p>
    <a href="https://github.com/linqfy/PivotKit/actions/workflows/build.yml"><img src="https://github.com/linqfy/PivotKit/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
    <a href="https://github.com/linqfy/PivotKit/releases"><img src="https://img.shields.io/github/v/release/linqfy/PivotKit?include_prereleases&label=release" alt="Latest release"></a>
    <a href="LICENSE"><img src="https://img.shields.io/github/license/linqfy/PivotKit" alt="MIT license"></a>
  </p>

  <p>
    <a href="#overview">Overview</a> ·
    <a href="#features">Features</a> ·
    <a href="#get-started">Get started</a> ·
    <a href="#write-a-first-mod">Write a mod</a> ·
    <a href="#build-from-source">Build</a> ·
    <a href="#documentation">Documentation</a> ·
    <a href="#troubleshooting">Troubleshooting</a>
  </p>
</div>

PivotKit loads Lua mods into the 32-bit Windows release of Pivot Animator
5.2.11. It gives mods a practical way to inspect published Delphi RTTI, call
Pivot methods, react to input, draw an overlay, and automate common editing
tasks.

> [!WARNING]
> PivotKit is an experimental code-injection tool. It can crash Pivot or lose
> unsaved work. Back up your `.piv` files before using it. Only `pivot.exe`
> 5.2.11, 32-bit, is supported.

> [!IMPORTANT]
>
> # Major Rewrite in Progress
>
> PivotKit is currently undergoing a **major rewrite** aimed at significantly expanding the project beyond its current Lua modding surface.
>
> The rewrite is focused on a cleaner and more reliable runtime, a much larger mapped Pivot API, improved native integration, better tooling for mod developers, and a stronger foundation for both Lua and more advanced mods.
>
> **The current release remains usable, but it should be considered legacy while this work is underway.** APIs, internals, documentation, and project structure may change substantially as the new version develops.
>
> Development is active. Expect large changes.


## Overview

PivotKit runs next to Pivot as a loader and an injected DLL. The DLL embeds Lua
5.4.8 and loads mods from `pivotkit/mods/` at startup. The shipped
`pivotlib` module provides a higher-level API for common tasks, while the raw
`pivot.*` API remains available when a mod needs lower-level access.

The project is designed for experimentation and personal automation. It is
not affiliated with Motusoft, Peter Bone, or Pivot Animator.

## Features

- Load Lua mods without rebuilding Pivot.
- Use `pivotlib` helpers for frames, playback, figures, UI, overlays, hooks,
  commands, and timers.
- Inspect published classes, methods, and fields through Delphi RTTI.
- Receive polled mouse and keyboard events without inline hooks on Pivot's
  FMX input handlers.
- Use the optional console, loopback TCP bridge, scene pinning, batch support,
  and figure-builder automation.

## Get started

### Install a release

1. Download the latest archive from [GitHub Releases](https://github.com/linqfy/PivotKit/releases/latest).
2. Extract it into the directory that contains `pivot.exe`.
3. Run `pivotkit-loader.exe`.
4. Check `pivotkit.log` beside `pivot.exe` if you need to troubleshoot startup.

The extracted layout should look like this:

```text
pivot.exe
pivotkit.dll
pivotkit-loader.exe
pivotkit/
└── mods/
    ├── 00_pivotlib.lua
    └── ...
```

The loader also accepts an explicit Pivot path:

```text
pivotkit-loader.exe "C:\Path\to\pivot.exe"
```

Use `-console` to open the optional command console. The loopback bridge is
available on `127.0.0.1:50077`; `tools/pivotctl.py` can send commands to it.

## Write a first mod

Create `pivotkit\mods\10_first_mod.lua`:

```lua
local pivotlib = require("pivotlib")

pivotlib.log("hello from PivotKit")
pivotlib.on_update(function()
    pivotlib.frame_status("Lua is running")
end)
```

Restart Pivot with the loader. The status bar and `pivotkit.log` should show
that the mod ran. The shipped files in `mods/` are runnable examples.

For the recommended API, start with the [pivotlib guide](docs/PIVOTLIB.md).
The [raw Lua API](docs/MOD_API.md) is available when you need direct access to
Pivot's runtime surface.

## Build from source

The native build requires Visual Studio 2022 with the **Desktop development
with C++** workload and an x86 toolchain.

```text
build.bat
tools\make_lua_test.cmd
bin\lua.exe tests\pivotlib_test.lua
bin\lua.exe tests\demo_smoke.lua
```

The build writes `pivotkit.dll` and `pivotkit-loader.exe` to `bin/`. See
[BUILDING.md](docs/BUILDING.md) for the full setup and the command for
regenerating the RTTI reference.

## Documentation

- [All docs](docs/README.md) — guides, the RE knowledge base, Ghidra analysis, and example mods.
- [Getting started](docs/GETTING_STARTED.md) — installation, first mod, and common fixes.
- [pivotlib guide](docs/PIVOTLIB.md) — the recommended high-level API.
- [Raw Lua API](docs/MOD_API.md) — the lower-level `pivot.*` surface.
- [RTTI reference](docs/reference/README.md) — generated methods, fields, and classes.
- [Building from source](docs/BUILDING.md) — MSVC setup, tests, and generated docs.
- [Architecture](docs/ARCHITECTURE.md) — runtime boundaries and compatibility rules.
- [Releasing](docs/RELEASING.md) — tags, changelog entries, and release archives.
- [Changelog](CHANGELOG.md) — user-visible changes by release.

## Troubleshooting

### Nothing happens

Run `pivotkit-loader.exe` from the directory that contains `pivot.exe` and
check `pivotkit.log`. Confirm that the executable is the 32-bit Pivot Animator
5.2.11 release.

### A mod does not load

Make sure the file is inside `pivotkit\mods\` and ends in `.lua`. Mod files
load in filename order, so `00_pivotlib.lua` should remain first.

### Pivot crashes

Do not use PivotKit with unsaved work. Restore the backup, confirm the Pivot
version, and temporarily remove custom mods to identify the failing one.

### The bridge does not respond

The default bridge address is `127.0.0.1:50077`. Start Pivot with the loader,
then run a command such as:

```text
python tools\pivotctl.py "pivotlib.frame()"
```

## Special Thanks

Special thanks to Hioh (Covellite), whose original request started this
project and whose feedback continues to shape it.

## License

PivotKit is [MIT-licensed](LICENSE). Pivot Animator itself is not distributed
with this project and remains the property of its respective owners.
