# PivotKit

PivotKit loads Lua mods into the 32-bit Windows release of Pivot Animator
5.2.11. Mods can inspect published Delphi RTTI, call Pivot methods, react to
input, draw an overlay, and automate common editing tasks.

> **Compatibility warning:** this is an experimental code-injection tool. It
> can crash Pivot or lose unsaved work. Back up your `.piv` files. Only
> `pivot.exe` 5.2.11, 32-bit, is supported.

## Install a release

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

## Documentation

- [Getting started](docs/GETTING_STARTED.md) — install, first mod, and common fixes.
- [pivotlib guide](docs/PIVOTLIB.md) — the recommended high-level API.
- [Raw Lua API](docs/MOD_API.md) — the lower-level `pivot.*` surface.
- [RTTI reference](docs/reference/README.md) — generated methods, fields, and classes.
- [Building from source](docs/BUILDING.md) — MSVC setup, tests, and generated docs.
- [Architecture](docs/ARCHITECTURE.md) — runtime boundaries and compatibility rules.
- [Releasing](docs/RELEASING.md) — tag, changelog, and archive workflow.
- [Changelog](CHANGELOG.md) — user-visible changes by release.

## Development commands

Visual Studio 2022 with the **Desktop development with C++** workload is
required for the native build.

```text
build.bat
tools\make_lua_test.cmd
bin\lua.exe tests\pivotlib_test.lua
bin\lua.exe tests\demo_smoke.lua
```

## Repository layout

```text
src/        Native loader and injected DLL
lua/        Vendored Lua 5.4.8 source
mods/       Runtime library and example mods
tests/      Lua tests using a mock Pivot API
tools/      RTTI and bridge utilities
docs/       Maintained guides and generated reference
bin/        Ignored local build output
```

PivotKit is [MIT-licensed](LICENSE) and is not affiliated with or endorsed by
Motusoft, Peter Bone, or Pivot Animator.
