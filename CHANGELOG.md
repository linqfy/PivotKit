# Changelog

All notable changes to this project are documented here.
This project is in **heavy beta** — expect breaking changes.

## [0.4.0] — console, TCP bridge, input events, scene pinning, batch, figure builder

- **Console (BepInEx-style)**: real Windows console window showing all
  `pivot.log` output with an interactive prompt. Enable with `pivotkit-loader
  -console`, via the `PIVOTKIT_CONSOLE` env var, or at runtime with
  `pivot.console(true)` / `pivotlib.console()`. Commands run on the main
  thread; bare pivotlib commands work (falls back to Lua eval).
- **TCP bridge (enabled by default)**: loopback server on `127.0.0.1:50077`.
  One text command per connection, runs on the main thread, replies with the
  result. `tools/pivotctl.py` is the Python client.
- **Input events**: `pivotlib.on_mouse_down/up/move` and `on_key_down/up`.
  **Polling-based** (per-frame `GetAsyncKeyState` + cursor position) so they
  can never crash pivot.exe. The initial hook-based implementation
  (inline-hooking FMX canvas/key methods) caused intermittent "write of access
  000000" crashes and was replaced.
- **Crash fixes**:
  - removed inline hooks on `EditPaintBoxMouseDown/Up/Move` and
    `FormKeyDown/Up` (the source of the access violations)
  - `hook_callback` is SEH-protected
  - bridge requests are leaked instead of freed (removes the use-after-free
    race with the 8s wait); `find_main_hwnd` caches the window handle
- **Scene pinning**: `pivot.peek(obj, offset, kind)`, `pivotlib.pin(class, probes)`
  (offset discovery) and `pivotlib.dump(obj, from, to)` — for pinning the
  private offsets of `TFigure` etc. (0 published fields).
- **Batch + undo**: `pivotlib.batch(fn)` commits `fn` as one undo action
  (best-effort `AssignUndo`).
- **Figure builder automation**: `pivotlib.open_figure_builder()` +
  `pivotlib.figure_builder()` (proxies the 88-method/82-field
  `TFigureBuilderForm`).
- **New demo mod** `mods/03_pivotlib_events.lua` (canvas click counter +
  HUD + `clicks` command).
- Tests: 79 checks; mock now supports invocable hooks, `peek` memory and
  `console`.

## [0.3.0] — hooks v2, overlay, mod manager, scene discovery

- **Hooks upgraded** (inline detour): callbacks now receive up to **six**
  arguments (2 register + 4 stack), Delphi **string args auto-converted** to
  Lua strings, and overrides can return a **string** (built as a constant
  Delphi string). Stack cleanup uses the original method's `ret N`.
- **New `pivot` API**:
  - `is_string` (Delphi string detection)
  - `find_instances(classType, max)` — heap-scan for multiple live instances
  - `enum_classes()` — every class in the image (~1868)
  - `reload([modname])` — hot-reload all mods or one
  - canvas **overlay**: `overlay_create/destroy/begin/text/line/rect/circle/commit`
- **`pivotlib` additions**:
  - hook wrappers with registry + `unhook_all()`, `reload()`
  - command registry: `register_command`, `run_command`, `commands`
  - keybindings: `bind(key, fn)`, `unbind_all()`
  - scene discovery: `scan`, `figures`, `figure_classes`, `probe`, `read_field`
  - programmatic SVG export: `export_svg`, `export_svg_nohandles`
  - overlay wrappers + `hud(fn)`
- **New demo mod** `mods/02_pivotlib_hud.lua` (F1 / `hud` command toggles a HUD).
- **Tooling**: `tools/dump_rtti.py --all-classes` → `docs/CLASSES.md` +
  `classes.json` (every class with its published method/field counts).
  Notable discovery: `TFigureBuilderForm` exposes 88 methods + 82 fields.
- **Robustness**: the published field-table parent walk is now guarded
  (`valid_field_table`: image-range + sane count) and `enum_*` are
  SEH-protected. Previously TMainForm's inherited field tables trailed into
  junk RTTI that hung `pivot.enum_fields` (found during live smoke testing).
- Tests extended to 60 checks (mock now covers find_instances, enum_classes,
  key polling, overlay, hooks).

## [0.2.0] — abstraction layer (pivotlib)

- **New `pivot` Lua API** (in `pivotkit.dll`):
  - object/pointer bridging: `ptr`, `address`, `is_object`, `get_ptr_field`
  - runtime introspection: `enum_methods`, `enum_fields`
  - typed fields: `get/set_string_field`, `get/set_single_field`,
    `get/set_double_field`, `get/set_bool_field`
  - string-aware calls: `call_string`, `call_string_ret`
- **`pivotlib`** (`mods/00_pivotlib.lua`): high-level Lua abstraction layer —
  proxy objects for the form & controls, lazy field/method resolution covering
  the **full published catalog** (159 methods + 146 fields), semantic helpers
  for playback, figures, zoom/camera, status bar, file/undo, plus
  multi-subscriber `on_update` and `every()` timers.
- **Demo mod** `mods/01_pivotlib_demo.lua`.
- **Catalog tooling**: `tools/dump_rtti.py` now emits `catalog.json` and
  `docs/CATALOG.md` (methods/fields grouped by area).
- **Tests**: `tests/mock_pivot.lua` + `tests/pivotlib_test.lua` run against a
  mock `pivot` API via a standalone `bin\lua.exe` (built by
  `tools/make_lua_test.cmd`) — no Pivot.exe needed.
- Docs: new `docs/PIVOTLIB.md`, updated `MOD_API.md` and `README.md`.

## [0.1.0] — initial beta

- Injects `pivotkit.dll` into 32-bit Pivot Animator 5.2.11 and runs Lua mods.
- Embeds Lua 5.4.8.
- `pivot` Lua API:
  - `log`, `get_main_form`, `call`, `get_field`/`set_field`
  - `on_update`, `frame_number`
  - `read_u32`, `write_u32`, `read_ptr`, `read_string` (raw memory)
  - `class`, `classname`, `find_instance`, `method_addr`, `field_offset`
  - `hook` / `unhook` (inline method interception: observe or override)
  - `key_press`, `key_down`, `sleep`
- CI/CD: GitHub Actions build + auto-published pre-release on tags.
- Docs: getting started, API reference, building.
