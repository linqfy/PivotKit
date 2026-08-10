# Building from Source

## Requirements

- Windows
- Visual Studio 2022 with the **"Desktop development with C++"** workload
  (this provides the x86 MSVC toolchain)
- Lua is vendored under `lua/` — no download needed

## Build

```
build.bat
```

The script locates Visual Studio via `vswhere` (or the `VSINSTALLDIR`
environment variable), loads the **x86** environment, compiles Lua into a static
lib, then builds:

- `bin\pivotkit.dll` — the injected host DLL
- `bin\pivotkit-loader.exe` — the launcher/injector

Both are 32-bit (PE machine type 0x14C), matching 32-bit `pivot.exe`.

## Layout

```
pivotkit/
├── build.bat          # MSVC x86 build
├── src/
│   ├── pivotkit.c     # host DLL: RTTI bridge, hooks, Lua embed
│   └── injector.c     # loader: launches pivot.exe suspended + injects
├── lua/               # Lua 5.4.8 source (vendored)
├── mods/              # sample mods (copied into pivotkit/mods/ at runtime)
├── tools/dump_rtti.py # dumps published methods/fields from pivot.exe
└── docs/              # these guides
```

## How it works (briefly)

Pivot Animator 5.2.11 is a 32-bit **Delphi 11 / FireMonkey** app with intact
published RTTI. `pivotkit.dll`:

1. Finds `TMainForm` at runtime by heap-scanning for its VMT.
2. Walks Delphi **published method / field tables** so mods can call or read
   anything by name.
3. Hooks `PeekMessageW` (IAT) for a per-frame Lua tick.
4. Supports inline method hooks (`E9` detour + trampoline) for behavior changes.

Version anchors live at the top of `src/pivotkit.c`. They were pinned against
5.2.11; other builds will need re-derivation.

## CI

`.github/workflows/build.yml` builds on `windows-latest`, verifies the artifacts
(32-bit), and — on `v*` tags — auto-publishes a GitHub Release (pre-release /
BETA) containing the DLL, loader, sample mods, and docs.
