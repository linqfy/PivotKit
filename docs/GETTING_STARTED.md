# Getting started

PivotKit runs Lua files inside Pivot Animator 5.2.11. It is useful for small
automations, runtime inspection, overlays, and experiments that would be hard
to build into the application itself.

## Before installing

- Use the **32-bit** Windows build of `pivot.exe` **5.2.11**.
- Make a backup of your `.piv` files.
- Keep the loader, DLL, and `pivotkit` folder beside `pivot.exe`.

This project injects code into another process. A mod or an unsupported Pivot
build can crash the application.

## Install a release

Download the latest archive, extract it into the directory containing
`pivot.exe`, and run `pivotkit-loader.exe`. The loader starts Pivot suspended,
injects `pivotkit.dll`, and resumes it.

If the loader cannot find Pivot, pass its full path:

```text
pivotkit-loader.exe "C:\Apps\Pivot Animator\pivot.exe"
```

Successful startup writes `pivotkit.log` beside `pivot.exe`. The log is the
first place to look when a mod does not load.

## Create a mod

Mods are Lua files under `pivotkit\mods\`. Use a filename with a numeric prefix
for predictable startup order; `00_pivotlib.lua` must load before mods that
call `require("pivotlib")`.

Create `pivotkit\mods\10_first_mod.lua`:

```lua
local pivotlib = require("pivotlib")

pivotlib.on_update(function()
    pivotlib.frame_status("Lua is running")
end)
```

Run Pivot through the loader and confirm that the status text changes. To log
diagnostics, call `pivotlib.log("message")`; the output goes to
`pivotkit.log` and to the optional console.

## Useful next steps

- Use [`pivotlib`](PIVOTLIB.md) for normal mod work.
- Use the [raw API](MOD_API.md) when you need pointers, typed fields, hooks, or
  runtime reflection.
- Run the examples in `mods/` and inspect the tests in `tests/`.
- Use [`pivotctl.py`](../tools/pivotctl.py) with `-console` or the bridge for
  one-line commands:

  ```text
  python tools\pivotctl.py "pivotlib.frame()"
  ```

## Troubleshooting

**Nothing happens.** Confirm that `pivotkit.dll`, `pivotkit-loader.exe`, and
`pivotkit\mods\00_pivotlib.lua` are beside the correct `pivot.exe`. Then read
`pivotkit.log` for `RTTI init failed`, `mainForm`, or mod errors.

**A mod reports a missing function.** Update the release files together. A
newer `pivotlib` file cannot add functions to an older injected DLL.

**Pivot crashes.** Remove the newest mod, restore the `.piv` backup if needed,
and verify that the executable is exactly version 5.2.11 32-bit. Avoid private
offsets until you have confirmed them with `pivotlib.probe` and `pivotlib.pin`.

**Will another Pivot version work?** No compatibility is promised. The native
RTTI layout and method addresses are tied to 5.2.11.
