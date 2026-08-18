# Building from source

## Requirements

- Windows
- Visual Studio 2022 with the **Desktop development with C++** workload
- The x86 MSVC toolchain provided by that workload
- Python 3 with `pefile` only if you regenerate the RTTI reference

Lua 5.4.8 is vendored under `lua/`; the native build does not download it.

## Build the native binaries

Run this from the repository root:

```text
build.bat
```

The script locates Visual Studio through `VSINSTALLDIR`, known installation
paths, or `vswhere`, then loads the x86 environment. It produces:

- `bin\pivotkit.dll` — injected host DLL
- `bin\pivotkit-loader.exe` — launcher and injector
- `bin\lua54.lib` — static Lua library used by the host and test runner

All three executables are built for x86 to match Pivot 5.2.11.

## Run the Lua tests

Build the standalone interpreter once:

```text
tools\make_lua_test.cmd
```

Then run both suites:

```text
bin\lua.exe tests\pivotlib_test.lua
bin\lua.exe tests\demo_smoke.lua
```

These tests use `tests\mock_pivot.lua`; they do not start Pivot and do not
exercise live process injection.

## Regenerate the RTTI reference

The generated files in `docs\reference\` describe the published RTTI found in
the supported `pivot.exe`. Install `pefile`, then run:

```text
python tools\dump_rtti.py path\to\pivot.exe --json --markdown --all-classes
```

The generator writes `catalog.json`, `CATALOG.md`, `classes.json`, and
`CLASSES.md` under `docs\reference\`. Do not hand-edit those files.

## Runtime boundaries

`src\pivotkit.cpp` contains the injected DLL. It discovers Pivot's published
RTTI, exposes the `pivot` Lua table, and schedules work on Pivot's main thread.
`src\injector.c` starts `pivot.exe` suspended and loads the DLL before resume.

The version anchors near the top of `src\pivotkit.cpp` are tied to Pivot
5.2.11 and Delphi 11. Supporting another build requires re-deriving those
anchors and validating the complete runtime surface.

## CI

`.github/workflows/build.yml` builds with MSVC x86, checks the PE architecture,
runs both Lua suites, uploads the binaries, and publishes a prerelease when a
`v*` tag is pushed. See [RELEASING.md](RELEASING.md) before creating a tag.
