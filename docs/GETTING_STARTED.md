# Getting Started

## What this is

`PivotKit` is a **code-injection mod loader** for [Pivot Animator](https://pivotanimator.net)
5.2.11 (32-bit). It loads **Lua** scripts ("mods") into the running app so you
can automate, observe, and modify what Pivot does — without touching its files.

> **⚠️ HEAVY BETA.** This injects code into `pivot.exe`. It may crash the app or
> lose work. Back up your `.piv` files. Only the 32-bit v5.2.11 build of Pivot is
> supported.

## Install

You need Pivot Animator 5.2.11 installed **or** just the portable folder. You
also need the two binaries from the release:

```
pivotkit.dll
pivotkit-loader.exe
```

1. Copy `pivotkit.dll` and `pivotkit-loader.exe` into the **same folder as
   `pivot.exe`**.
2. Create a `pivotkit\mods\` folder next to `pivot.exe` (the release zip
   already includes sample mods there).
3. Double-click `pivotkit-loader.exe` instead of `pivot.exe`.

You should see Pivot open normally. If it worked, a `pivotkit.log` file appears
next to `pivot.exe`.

## Write your first mod

Every `*.lua` file in `pivotkit\mods\` is loaded ~2 seconds after startup.

```lua
-- mods/hello.lua
pivot.log("hello from my first mod!")
local form = pivot.get_main_form()
pivot.log("main form: " .. tostring(form))
```

Reload Pivot via the loader and check `pivotkit.log`.

## What you can do

- **Call published methods** on the main form and other objects:
  ```lua
  pivot.call(form, "SetFrameNumber", 1)
  ```
- **Read/write fields**:
  ```lua
  local play = pivot.get_field(form, "PlayButton")
  pivot.set_field(form, "SomeField", 42)
  ```
- **Hook methods** (observe or override):
  ```lua
  pivot.hook(form, "SetNumFrames", function(self, n)
      pivot.log("frame count set to " .. n)
      return nil           -- nil lets the original run
      -- return 9000       -- a number replaces the result
  end)
  ```
- **Run per-frame logic**:
  ```lua
  pivot.on_update(function(frame)
      if frame % 120 == 0 then pivot.log("tick " .. frame) end
  end)
  ```

See `docs/MOD_API.md` for the full API reference, and `docs/BUILDING.md` if you
want to compile from source.

## FAQ

**Why does nothing happen?**
- Check `pivotkit.log` next to `pivot.exe` for errors.
- Make sure you ran `pivotkit-loader.exe`, not `pivot.exe`.

**My mod errored.**
Every mod is loaded independently — a crash or error in one mod is reported in
the log and does not stop the others.

**Will this work on Pivot 4 / 6 / 64-bit?**
No. It targets 32-bit Pivot Animator 5.2.11 specifically. The RTTI layout
offsets are version-pinned.

**Is this official?**
No. It is an independent community project, unaffiliated with Motusoft / Peter
Bone / Pivot Animator.
