# Changelog

All notable changes to this project are documented here.
This project is in **heavy beta** — expect breaking changes.

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
