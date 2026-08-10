# PivotKit — Lua mod loader for Pivot Animator

> ## ⚠️ HEAVY BETA
>
> This project injects code into Pivot Animator. It is experimental: it may
> crash the app or lose unsaved work. **Back up your `.piv` files.** Only the
> **32-bit** `pivot.exe` **5.2.11** is supported.

PivotKit is a code-injection mod loader for [Pivot Animator](https://pivotanimator.net)
5.2.11. It embeds **Lua 5.4.8** into the running app and runs user mods from a
`pivotkit/mods/` folder — letting you automate, observe, and modify Pivot's
behavior at runtime using Pivot's own **published Delphi RTTI**.

This project is not affiliated with Motusoft / Peter Bone.

## Quick start

1. Grab `pivotkit.dll` + `pivotkit-loader.exe` from the [latest release](https://github.com/linqfy/PivotKit/releases/latest).
2. Copy them next to `pivot.exe`.
3. Put mods in `pivotkit\mods\` (sample mods included if you want quickly try it!).
4. Run `pivotkit-loader.exe` instead of `pivot.exe`.

Check `pivotkit.log` (created next to `pivot.exe`) to confirm it loaded.

## What mods can do

```lua
local form = pivot.get_main_form()

-- call a published method
pivot.call(form, "SetFrameNumber", 1)

-- read a component field
local play = pivot.get_field(form, "PlayButton")

-- hook a method: observe (return nil) or override (return a value)
pivot.hook(form, "SetNumFrames", function(self, n)
    pivot.log("frames set to " .. n)
    return nil
end)

-- per-frame logic
pivot.on_update(function(frame) ... end)
```

## Docs

- [Getting Started](docs/GETTING_STARTED.md)
- [Lua API reference](docs/MOD_API.md)
- [Building from source](docs/BUILDING.md)
- [Changelog](CHANGELOG.md)

## Repository layout

```
pivotkit/
├── src/        # pivotkit.c (host DLL) + injector.c (loader)
├── lua/        # Lua 5.4.8 (vendored)
├── mods/       # sample Lua mods
├── tools/      # RTTI dump helper
├── docs/       # guides
└── build.bat   # MSVC x86 build
```

## Building

Requires Visual Studio 2022 (Desktop development with C++). Then:

```
build.bat
```

Outputs `bin\pivotkit.dll` and `bin\pivotkit-loader.exe`. See
[docs/BUILDING.md](docs/BUILDING.md).

## License

[MIT](LICENSE). Pivot Animator itself is copyright Motusoft GmbH / Peter Bone and
is not distributed here.

## Disclaimer

**Heavy beta.** Use at your own risk. This project is a community reverse
engineering / modding effort and is not affiliated with or endorsed by the
creators of Pivot Animator. 

## Special Thanks

A special thanks to **Hioh (Covellite)** for requesting this project and being the reason it exists.

Since this project was originally created at his request, its development will be **user-driven**: updates and new features will primarily be made as Hioh uses the project and provides feedback on what he needs or wants added.

In other words, this project will evolve alongside his use of it.