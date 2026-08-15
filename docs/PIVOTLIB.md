# PIVOTLIB.md — high-level Lua abstraction layer

`pivotlib` is the recommended way to write mods. It wraps the low-level
`pivot.*` module (see [MOD_API.md](MOD_API.md)) with proxy objects and typed,
semantic helpers — so you rarely touch raw pointers or method names by hand.

It ships as `mods/00_pivotlib.lua` (loaded first) and registers itself in
`package.preload`, so every mod can simply use the global `pivotlib`, or:

```lua
local pivotlib = require("pivotlib")
```

> Requires the **newer `pivotkit.dll`** (v0.2+). If a function here errors with
> "attempt to call a nil value", the injected DLL is the old one.

---

## Proxy objects

The core abstraction. `pivotlib.main()` returns a proxy for the live
`TMainForm`; `pivotlib.obj(x)` wraps any object pointer (integer or userdata).

On a proxy, **published fields** are read lazily and **published methods** are
callable. Object fields (controls) come back as proxies too, so you can chain:

```lua
local f = pivotlib.main()

f:SetFrameNumber(3)          -- call a published method (colon or dot)
local tween = f:GetFrameTween()
local play = f.PlayButton     -- a TButton proxy
```

Special proxy members:

| Member | Meaning |
|--------|---------|
| `obj.Class` | class name (e.g. `"TMainForm"`) |
| `obj.Address` | raw instance address as an integer |
| `obj.Methods` | sorted list of published method names |
| `obj.Fields` | sorted list of published field names |
| `obj:MethodName(...)` | call a published method; result is auto-wrapped (object → proxy) |
| `obj.FieldName` | read a published field (object → proxy, else number) |
| `obj.FieldName = v` | write a published field (takes a number or a proxy) |

> `pivotlib.obj` also accepts the integer/`userdata` values returned by the raw
> `pivot.*` API, so mixing both styles is fine.

---

## Semantic API (all areas)

### Playback & frames
| Function | Description |
|----------|-------------|
| `play()` / `stop()` | start / stop playback |
| `next_frame()` | advance one frame |
| `set_frame(n)` | jump to frame `n` |
| `set_num_frames(n)` | set the total frame count |
| `frame()` | current frame (parsed from the status bar, or hook-tracked) |
| `num_frames()` | total frames (parsed from the status bar) |
| `tween()` / `set_tween(v)` | get / set the current frame's tween value |
| `track_frames(true\|false)` | hook `SetFrameNumber` so `frame()` is exact |

### Figures & selection
`select_all()`, `duplicate()`, `flip()`, `center()`, `add_figure()`,
`delete_figure()`, `join()`, `copy_figure()`, `paste_figure()`, `edit_type()`,
`reset_pose()`

### Canvas / zoom / camera
`zoom_in()`, `zoom_out()`, `zoom_50/100/200()`, `zoom_selected()`,
`zoom_anim()`, `show_cam()`, `reset_cam()`, `copy_cam()`, `paste_cam()`,
`align_cam()`, `apply_cam()`, `center_cam_figs()`, `zoom_cam()`

### Status bar & UI
| Function | Description |
|----------|-------------|
| `set_text(control, text)` | set a control's text via `SetText` (falls back to the `Text` field) |
| `get_text(control)` | read a control's text via `GetText` |
| `figure_status(t)`, `frame_status(t)`, `segment_status(t)`, `zoom_label(t)`, `frame_label(t)`, `tween_label(t)` | write status / label texts |

### File & undo
`undo()`, `redo()`, `new_animation()`, `save()`, `save_as()`, `open_dialog()`,
`export()`, `load_background()`, `load_sprite()`, `load_figure()`,
`load_project(path)` (best-effort, string-aware), `export_svg(path)`,
`export_svg_nohandles(path)` (best-effort SVG export without dialogs)

### Events
| Function | Description |
|----------|-------------|
| `on_update(fn)` | per-frame callback (multiple subscribers supported) |
| `every(ms, fn)` | run `fn` roughly every `ms` milliseconds (~60fps) |
| `menu_button(label, fn)` / `remove_menu_button()` | floating button on Pivot |

### Keybindings
| Function | Description |
|----------|-------------|
| `bind(key, fn)` | run `fn` on the key's press edge; `key` is a VK code or name (`"P"`, `"F5"`, `"SPACE"`, `"UP"`, ...) |
| `unbind_all()` | remove all bindings |

```lua
pivotlib.bind("F1", function() pivotlib.log("pressed F1") end)
```

### Hooks & mod manager
| Function | Description |
|----------|-------------|
| `hook(obj, method, fn)` | like `pivot.hook`, but registered so a reload can undo it |
| `unhook(obj, method)` | remove one registered hook |
| `unhook_all()` | remove every registered hook (used by `reload`) |
| `register_command(name, fn)` | register a named command |
| `run_command(name, ...)` | run a command (returns `true, result`) |
| `commands()` | sorted list of registered command names |
| `reload(modname?)` | re-run one mod (`"foo"` → `foo.lua`) or all; unhooks first |

```lua
pivotlib.register_command("hello", function() return "world" end)
pivotlib.run_command("hello")          -- true, "world"
pivotlib.reload("02_pivotlib_hud")     -- hot reload one mod
```

### Scene / introspection (runtime discovery)
| Function | Description |
|----------|-------------|
| `scan(classname_or_obj, max)` | proxies for live instances of a class (by name, classType, or sample object) |
| `figures()` | best-effort live figure instances (classes whose name contains "Figure") |
| `figure_classes()` | class names containing "Figure" |
| `probe(classname)` | `(proxy, {field = value, ...})` for the first instance (type-detected reads) |
| `read_field(obj, name)` | read a field: object → proxy, Delphi string → string, else raw value |

```lua
for _, fig in ipairs(pivotlib.figures()) do
    pivotlib.log("figure at " .. tostring(fig))
end
```

> `TFigure`/frame classes expose **no published fields** (0/0 in
> `docs/CLASSES.md`), so `pos()`/`angle()` need private offsets. Start with
> `pivotlib.probe("TFigure")` at runtime to dump instance memory and pin them.

### Canvas overlay (HUD)
| Function | Description |
|----------|-------------|
| `overlay_create()` / `overlay_destroy()` | create / destroy the drawing surface |
| `overlay_begin()` / `overlay_commit()` | start / blit a frame |
| `overlay_text(x, y, str, size, argb)` | draw text (window pixels, ARGB) |
| `overlay_line(x1, y1, x2, y2, argb, width)` | draw a line |
| `overlay_rect(x1, y1, x2, y2, argb)` | draw an outlined rect |
| `overlay_circle(x, y, r, argb)` | draw an outlined circle |
| `hud(fn)` | convenience: draw `fn` every frame (begin/draw/commit) |

```lua
pivotlib.hud(function()
    pivotlib.overlay_text(10, 8, "frame " .. tostring(pivotlib.frame()), 13, 0xFFFFFFFF)
    pivotlib.overlay_line(0, 0, 100, 100, 0x80FF0000, 1)
end)
```

### Passthroughs
`log(...)`, `sleep(ms)`, `key_press(vk)`, `key_down(vk)`, `window_rect()`,
`sprite(path)`, `sprite_move(h,x,y)`, `sprite_velocity(h,vx,vy)`,
`sprite_bounce(h,b)`, `sprite_show(h)`, `sprite_destroy(h)`

---

## Typed access

When you have a proxy and know a published field's type:

```lua
local sp = f.TweenSpinEdit
pivotlib.get_number(sp, "SomeFloatField", "double")   -- "single"|"double"
pivotlib.set_number(sp, "SomeFloatField", 1.5, "double")
pivotlib.get_string(sp, "Caption")
pivotlib.set_string(sp, "Caption", "hello")
pivotlib.get_bool(sp, "Checked")
pivotlib.set_bool(sp, "Checked", true)
```

For **methods** taking a Delphi string (or returning one), use:

```lua
pivotlib.call_method_string(f, "LoadProject", "C:\\path\\to.piv")
local s = pivotlib.call_method_string_ret(label, "GetText")
```

---

## Introspection

Everything is data-driven: `pivotlib` builds its catalog at runtime from the
injected DLL's `pivot.enum_methods` / `pivot.enum_fields`, so the full
published surface (159 methods + 146 fields on `TMainForm`) is covered without
hand-written wrappers. Inspect it:

```lua
local f = pivotlib.main()
for _, name in ipairs(f.Methods) do pivotlib.log(name) end
```

See [CATALOG.md](CATALOG.md) for the full reference, and the raw API in
[MOD_API.md](MOD_API.md).
