# MOD_API.md — Lua API reference

The injected DLL exposes one Lua global module, `pivot`. All functions run on
Pivot's main (FMX) thread, so calling Delphi methods from them is safe.

Objects (e.g. the main form, components) are passed to Lua as `lightuserdata`
values — basically raw pointers. `tostring` shows them as `userdata: 0x...`.

---

## Core

### `pivot.get_main_form()`
Returns the live `TMainForm` instance, or `nil` if it could not be found.

```lua
local form = pivot.get_main_form()
```

### `pivot.call(obj, "MethodName", arg1, arg2, ...)`
Calls a **published method** by name using Delphi's register calling
convention. Supports up to 32 integer/pointer arguments and returns the 32-bit
result.

```lua
local ok, res = pcall(pivot.call, form, "SetFrameNumber", 1)
```

> Methods can raise Delphi exceptions (e.g. before a project is loaded).
> Always wrap calls in `pcall`.

### `pivot.get_field(obj, "FieldName")` / `pivot.set_field(obj, "FieldName", value)`
Reads or writes a **published field** (32-bit). TMainForm exposes ~146
published fields — mostly the FMX controls on the form.

```lua
local play = pivot.get_field(form, "PlayButton")
pivot.set_field(form, "FrameNumber", 3)
```

### `pivot.on_update(function(frame) ... end)`
Registers a per-frame callback. It runs on the main thread on every message-loop
tick and receives the current frame counter.

```lua
pivot.on_update(function(frame)
    if frame % 60 == 0 then pivot.log("tick " .. frame) end
end)
```

### `pivot.frame_number()`
Returns the current update-frame counter.

---

## Method hooking

### `pivot.hook(obj, "MethodName", function(self, arg1, arg2) ... end)`
Intercepts a published method **inline**. The function is called with the
object and up to two register arguments:

- return `nil` → the original method runs afterwards (observe)
- return a number → that value becomes the result and the original is **skipped** (override)

```lua
-- observe
pivot.hook(form, "SetNumFrames", function(self, n)
    pivot.log("frames set to " .. n)
    return nil
end)

-- override
pivot.hook(form, "GetFrameTween", function(self)
    return 42
end)
```

### `pivot.unhook(obj, "MethodName")`
Restores the original method. Returns `true` if a hook was removed.

```lua
pivot.unhook(form, "GetFrameTween")
```

> Hooks are not re-entrant — do not call the same method from inside its own
> hook.

---

## Reflection helpers

| Function | Description |
|----------|-------------|
| `pivot.class("TClassName")` | classType (VMT address) for a class by name, or `nil` |
| `pivot.classname(obj)` | class name of an object (best-effort) |
| `pivot.find_instance(classType)` | heap-scan for the first live instance of a class |
| `pivot.method_addr(obj, "Name")` | address of a published method, or `nil` |
| `pivot.field_offset(obj, "Name")` | raw instance offset of a published field |

```lua
local ct = pivot.class("TMainForm")
local anyForm = pivot.find_instance(ct)
pivot.log(pivot.classname(anyForm))
```

---

## Raw memory

For reaching **private** fields or arbitrary data.

| Function | Description |
|----------|-------------|
| `pivot.read_u32(addr)` | read 32-bit value |
| `pivot.write_u32(addr, value)` | write 32-bit value (SEH-guarded) |
| `pivot.read_ptr(addr)` | read pointer-sized value |
| `pivot.read_string(addr)` | read a Delphi shortstring (length-prefixed) |

All reads are exception-guarded: an invalid address returns `nil` instead of
crashing Pivot.

---

## Input & utility

| Function | Description |
|----------|-------------|
| `pivot.key_press(vk)` | synthesize a key press (e.g. `0x20` = Space) |
| `pivot.key_down(vk)` | poll whether a virtual key is held |
| `pivot.sleep(ms)` | sleep the current thread |
| `pivot.log(...)` | write a line to `pivotkit.log` |

## Sprites & UI (demo feature)

Layered always-on-top sprites and a floating button on the Pivot window.

| Function | Description |
|----------|-------------|
| `pivot.sprite("path.png")` | load an image into a sprite; returns a 1-based handle (or `nil`) |
| `pivot.sprite_move(handle, x, y)` | move sprite to screen coords |
| `pivot.sprite_velocity(handle, vx, vy)` | pixels moved per frame |
| `pivot.sprite_bounce(handle, true\|false)` | bounce off the Pivot window edges |
| `pivot.sprite_show(handle)` / `pivot.sprite_hide(handle)` | show / hide the sprite |
| `pivot.sprite_pos(handle)` | current `x, y` |
| `pivot.sprite_destroy(handle)` | remove the sprite window |
| `pivot.window_rect()` | Pivot client area in screen coords: `left, top, right, bottom` |
| `pivot.add_menu_button(label, fn)` | floating button (top-right); `fn` runs on click |
| `pivot.remove_menu_button()` | remove the button |

Image paths are resolved relative to the folder containing `pivot.exe`. Up to 16
sprites can be alive at once.

```lua
local logo = pivot.sprite("pivotkit/mods/silly_cat.jpg")
local l, t, r, b = pivot.window_rect()
pivot.sprite_move(logo, (l + r) / 2, (t + b) / 2)
pivot.sprite_velocity(logo, 3, 2)
pivot.sprite_bounce(logo, true)
```

## Notes & limits

- Arguments are integers/pointers only. Methods that take Delphi strings or
  floats need a thin C shim (the raw memory helpers can cover simple cases).
- `pivot.classname` is best-effort; a few FMX framework classes don't expose a
  readable class name via the VMT slot we probe.
- The loader targets **32-bit Pivot Animator 5.2.11** only.
