# PivotKit runtime modules (src/)

One merged runtime - no v1/v2 split. `pivotkit.cpp` is the proven host
(entry point, Lua host, console/bridge, overlay/sprites, published-method
hooks); the `pk_*` modules are the typed C API layer, compiled and linked
into the same pivotkit.dll:

| Module | Files | Purpose |
|--------|-------|---------|
| core | pk_core.h/.c | logging, version gate (5.2.11 marker check), image base |
| rtti | pk_rtti.h/.c | Delphi RTTI navigator + register-convention invoker (SEH-hardened) |
| hooks | pk_hooks.h/.c | patch engine: LDE-safe stolen bytes, before/after/override, VMT-slot + IAT patches, tick registry |
| api | pk_api.h/.c | typed surface: TFrameSequence frames, figures, vertices, camera (offsets from include/pivot/ bindings) |
| host | pivotkit.cpp, injector.c | DllMain, Lua 5.4 host, ~60 pivot.* APIs incl. call_addr/hook_addr, console, TCP bridge, PeekMessageW tick, bytecode-only mod loader |

Glue: `pivot.pk_frame_count` (Lua) -> `pk_frame_count()` (pk_api) proves the
module path is live in the shipped DLL. The host's hook engine remains the
runtime-proven one; pk_hooks is the hardened engine for the next milestone
(its dispatch stub needs live validation before pivot.hook switches to it).

Build: build.bat (or build_one.cmd for a quick host+modules rebuild).
Regression: tests/ + run_bindings_test.cmd.
