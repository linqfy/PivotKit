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

### `pivot.get_ptr_field(obj, "FieldName")`
Like `pivot.get_field`, but returns a `lightuserdata` instead of an integer —
so you can pass the value straight back into `pivot.call` / `pivot.hook`
(objects stay objects).

```lua
local play = pivot.get_ptr_field(form, "PlayButton")
pivot.call(play, "SetEnabled", 1)
```

### Object / pointer bridging
| Function | Description |
|----------|-------------|
| `pivot.ptr(int)` | reinterpret an address as `lightuserdata` (so raw integers can be used as objects) |
| `pivot.address(obj)` | the integer address of a userdata (idempotent on integers) |
| `pivot.is_object(v)` | `true` if `v` (userdata or integer) looks like a live Delphi object (its VMT is inside pivot.exe) |
| `pivot.is_string(v)` | `true` if `v` looks like a Delphi `UnicodeString` |

```lua
local play = pivot.ptr(pivot.get_field(form, "PlayButton"))  -- int -> object
if pivot.is_object(play) then pivot.call(play, "SetEnabled", 0) end
```

### Mod reload
| Function | Description |
|----------|-------------|
| `pivot.reload()` | re-run every `*.lua` in `pivotkit/mods/` |
| `pivot.reload("name")` | re-run a single mod (`name.lua`) |

`pivot.reload` does **not** clear hooks — call `pivotlib.unhook_all()` first (the
`pivotlib.reload()` helper does this for you).

### Canvas overlay (HUD)
Draw over Pivot's own canvas (window pixel coordinates, ARGB colors) via a
transparent layered window — no FMX internals touched.

| Function | Description |
|----------|-------------|
| `pivot.overlay_create()` / `pivot.overlay_destroy()` | create / destroy the overlay surface |
| `pivot.overlay_begin()` | clear the current frame's primitives |
| `pivot.overlay_text(x, y, str, size, argb)` | draw text |
| `pivot.overlay_line(x1, y1, x2, y2, argb, width)` | draw a line |
| `pivot.overlay_rect(x1, y1, x2, y2, argb)` | draw an outlined rectangle |
| `pivot.overlay_circle(x, y, r, argb)` | draw an outlined circle |
| `pivot.overlay_commit()` | blit the frame to the screen |

```lua
pivot.overlay_create()
pivot.on_update(function()
    pivot.overlay_begin()
    pivot.overlay_text(10, 10, "frame " .. tostring(pivot.frame_number()), 13, 0xFFFFFFFF)
    pivot.overlay_commit()
end)
```

### Runtime introspection
| Function | Description |
|----------|-------------|
| `pivot.enum_methods(obj)` | array of published method names |
| `pivot.enum_fields(obj)` | array of published field names (includes inherited) |
| `pivot.enum_classes()` | array of every class name found in the image (~1868) |
| `pivot.find_instances(classType, max)` | heap-scan for up to `max` live instances of a class |

```lua
for _, name in ipairs(pivot.enum_methods(form)) do pivot.log(name) end
local figs = pivot.find_instances(pivot.class("TFigure"), 64)
```

These are what the `pivotlib` abstraction layer uses to build its data-driven
catalog and to discover live objects (figures, frames, other forms) at runtime.

### Typed fields (string / float / double / bool)
| Function | Description |
|----------|-------------|
| `pivot.get_string_field(obj, "Name")` | read a Delphi `string` (UTF-8) |
| `pivot.set_string_field(obj, "Name", str)` | write a Delphi string (the app takes ownership; old value is leaked) |
| `pivot.get_single_field` / `pivot.set_single_field` | Delphi `Single` (32-bit float) |
| `pivot.get_double_field` / `pivot.set_double_field` | Delphi `Double` (64-bit float) |
| `pivot.get_bool_field` / `pivot.set_bool_field` | Delphi `Boolean` (1 byte) |

### String-aware method calls
| Function | Description |
|----------|-------------|
| `pivot.call_string(obj, "Method", str, ints...)` | pass a Delphi string as the first argument (freed after the synchronous call) |
| `pivot.call_string_ret(obj, "Method", ints...)` | call a method whose **result** is a Delphi string; returns a Lua string |

```lua
pivot.call_string(form, "LoadProject", "C:\\demo\\run.piv")
local label = pivot.get_ptr_field(form, "FrameStatus")
pivot.call_string(label, "SetText", "Frame 1 of 10")
local text = pivot.call_string_ret(label, "GetText")
```

> **Safety note:** writing Delphi strings into fields (or passing them to
> methods that may store them) hands ownership to the app. Pivot will free them
> on the next assignment — safe as long as Pivot is the only owner. Never free
> a string you wrote into a field.

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

### `pivot.hook(obj, "MethodName", function(self, arg1, arg2, s1, s2, s3, s4) ... end)`
Intercepts a published method **inline**. The callback is called with the object
and up to **six** arguments: the two register args (`arg1`/`arg2`) plus the
first four stack args (`s1`..`s4`). Delphi string arguments are detected and
converted to Lua strings automatically; everything else arrives as an integer.

- return `nil` → the original method runs afterwards (observe)
- return a number → that value becomes the result and the original is **skipped** (override)
- return a string → a Delphi string is built and returned (override) — the app never frees it

```lua
-- observe (string arg arrives as a Lua string)
pivot.hook(form, "SetNumFrames", function(self, n)
    pivot.log("frames set to " .. n)
    return nil
end)

-- override with a string result
pivot.hook(label, "GetText", function(self)
    return "hooked!"
end)
```

### `pivot.unhook(obj, "MethodName")`
Restores the original method. Returns `true` if a hook was removed.

```lua
pivot.unhook(form, "GetFrameTween")
```

> Hooks are not re-entrant — do not call the same method from inside its own
> hook. Overriding a method with stack args cleans the stack the way the real
> method would (`ret N` is derived from the original epilogue).

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

- Integer/pointer arguments are handled natively; Delphi strings and floats are
  covered by the typed helpers above (`get_*_field`, `call_string`, ...).
- `pivot.hook` callbacks receive the object plus up to **six** arguments (two
  register + four stack); methods with 7+ args are not fully captured. String
  arguments are auto-detected.
- `pivot.classname` is best-effort; a few FMX framework classes don't expose a
  readable class name via the VMT slot we probe.
- The loader targets **32-bit Pivot Animator 5.2.11** only.

---

## Abstraction layer

For most mods, prefer `pivotlib` — the Lua library in
[PIVOTLIB.md](PIVOTLIB.md) — which wraps this raw API with proxy objects and
semantic helpers, and auto-covers the full published catalog
([CATALOG.md](CATALOG.md)).
