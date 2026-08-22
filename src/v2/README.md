# PivotKit 2 runtime (src/v2)

Modular rewrite of the monolithic legacy `src/pivotkit.cpp`. Each module maps
to one SDK-DESIGN.md component:

| Module | Files | Purpose |
|--------|-------|---------|
| core | `include/pk_core.h` | base types, logging, version gate, VMT slot constants |
| rtti | `include/pk_rtti.h`, `rtti.c` | Delphi RTTI navigator + register-convention invoker (SEH-hardened port of the proven v1 code) |
| hooks | `include/pk_hooks.h`, `hooks.c` | patch engine v2: before/after/override function patches with length-decoded stolen bytes, VMT-slot patches, IAT patches, per-frame tick registry |
| api | `include/pk_api.h`, `api.c` | typed Pivot surface: MainForm, TFrameSequence frames, figures, vertices, camera, events — offsets from the generated bindings |

Build: include `include/` and `../../include/pivot/` in the include path,
compile the `.c` files with MSVC (32-bit, matches the host process). Link
against the Lua host + loader exactly as v1 does (see ../build notes in docs/).

Status: headers + first implementations; the hook engine's stub generator and
the RTTI walker reuse logic proven in v1 at runtime. Next: Lua binding layer
over pk_api, event bus, menu injection (see research/sdk-notes/SDK-DESIGN.md
milestones).
