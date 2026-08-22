# PivotKit 2 — SDK design (BepInEx-class for a Delphi/FMX native app)

Goal: make deep modification of Pivot Animator 5.x possible **without per-mod
reverse engineering**, with the ergonomics modders know from BepInEx:
deterministic loading, a typed API surface, safe patching, config, logging,
and a console/IPC bridge.

## What BepInEx gives (the bar to clear)

| BepInEx feature | PivotKit 2 equivalent |
|---|---|
| Plugin discovery & dependency order | mod packages with manifest (name, version, deps, pivot version range) |
| Harmony prefix/postline/finalizer patching | **delphi-patch engine**: inline detours with before/after/override semantics + VMT-slot patches + IAT patches |
| Interop assemblies generated from game code | **bindings generated from the RE database** (`classes_full.json` → C headers → Lua/C#-facing API) |
| Unified logging & console | built-in logger + in-app console + TCP bridge (proven in v1) |
| Config files with UI | INI/JSON config + auto-generated settings dialog |
| Runtime inspector | object graph browser driven by the field database |
| Entry events (plugin load, scene load, update) | lifecycle events (see below) |

## Architecture (modules, all C, mirroring the DB)

```
pivotkit-loader.exe        bootstrap injector (exists, v1)
pivotkit.dll
  core/      loader, manifest parsing, logging, config, version gate
  rtti/      Delphi RTTI navigator (v1, hardened): class lookup, published
             method/field resolution, init-table field offsets
  hooks/     detour engine: register-convention stubs, VMT patches, IAT
             patches, patch registry + uninstall
  api/       typed Pivot surface: Application, MainForm, Animation, Frame,
             Figure, FigureType, Canvas, Camera, Selection, Menus, Events
  events/    lifecycle dispatch: on_load, on_form_ready, on_frame_change,
             on_playback_*, on_figure_*, on_file_*, on_render, on_tick
  bridge/    TCP 50077 console (v1) + structured JSON protocol
  lang/      Lua 5.4 host (v1) — first-class scripting backend
```

## The typed API (shapes; backed by the RE database)

```c
// generated + hand-annotated from research/mappings/classes_full.json
typedef struct PkFigure {      // TFigure (FigureUnit)
    uint32_t vmt;              // +0x00
    PkRect   extent;           // +0x04  TRect
    PkRectF  extent_f;         // +0x14  TRectF
    uint32_t color;            // +0x24  TAlphaColor
    uint8_t  trans;            // +0x28
    float    scale;            // +0x2C
    uint16_t figure_type;      // +0x30
    PkDynArray vertices;       // +0x34  TArray<TFigVertex>
    PkDynArray segments;       // +0x38  TFigSegments
    uint8_t  flipped;          // +0x3C
    ...
} PkFigure;

typedef struct PkFrame {       // TFigures (FiguresUnit) = one frame
    uint16_t num_figures;      // +0x04
    uint16_t background;       // +0x06
    uint16_t background2;      // +0x08
    uint8_t  background_ratio; // +0x0A
    uint16_t frame_tween;      // +0x0C
    PkDynArray figures;        // +0x10  array of TFigure
    PkDynArray draw_order;     // +0x14  TArrayOfWord
    ...
    PkCamera camera;           // +0x38
} PkFrame;
```

Lua view (v1 compatibility, now typed):

```lua
local app  = pivot.app
local doc  = app.animation          -- animation/document
local f    = doc:frame(3)           -- PkFrame
f.tween = 5
local fig = f:figure(2)             -- PkFigure
fig.color = 0xFF3040
fig:move(10, -4)
pivot.events.on_frame_change(function(idx) ... end)
pivot.menus.add("Tools/My Tool", function() ... end)  -- native FMX menu item
```

## Patch engine semantics (the Harmony analog)

```lua
pivot.patch.before("MainUnit.TMainForm.SelectFrame", function(self, idx) ... end)
pivot.patch.after("MainUnit.TMainForm.SetNumFrames", function(self, n, retval) ... end)
pivot.patch.override(addr_or_symbol, my_replacement)  -- full control
pivot.patch.vmt("FigureUnit.TFigure", "Destroy", fn)  -- VMT-slot patch
pivot.patch.iat("user32.dll", "PeekMessageW", fn)     -- proven path
```

Requirements (from research/hooks/hook-points.md): register-convention
trampolines, stolen-byte relocation with a length-disassembler (v1 copied
blind — fix), callee-cleanup-aware return override, main-thread install
window, per-class VMT copies for scoped virtual patches.

## Native UI integration (the BepInEx "looks native" bar)

FMX menus are built from the form's component tree (`TMainForm.MainMenu1`
@ +0x2F8). The SDK walks published fields to find menu owners, then uses
FMX's own `TMenuItem` creation APIs via RTTI-callable published methods or
direct construction (needs: FMX class construction path — TODO item).
Target: `pivot.menus.add("File/Export/My Format…", handler)` producing a real
FMX menu item with the app's styling, plus `pivot.ui.dialog`, `pivot.ui.panel`.

## Versioning & distribution

* Every mapping row carries the pivot version + binary hash prefix; loader
  gates on hash match (fail loudly, never guess offsets).
* `classes_full.json` is the single source of truth; `tools/gen_bindings.py`
  regenerates headers/Lua docs from it; mods declare supported versions.
* Mods: `pivotkit/mods/<name>/mod.lua` + `mod.json` manifest (id, name,
  version, requires, pivot_versions, load order hint).

## Milestones

1. ✅ RE database (2516 classes, private field maps) — this repo, research/
2. Bindings generator + headers (tools/gen_bindings.py → include/pivot/)
3. Hook engine v2 (LDR-based trampolines, patch registry)
4. Typed API layer (Application/MainForm/Frame/Figure first)
5. Native menu/UI injection
6. Event bus + config + inspector
7. Docs site + mod templates
