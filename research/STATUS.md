# STATUS — Pivot Animator 5.2.11 RE

Binary analyzed: `pivot.exe` 5.2.11, SHA-256 prefix `2c7911d3303cc28de2ac016c3f757508…`.
Ghidra project: `PivotAnim` (investigation workspace), image base `0x400000`,
compiler spec `borlanddelphi` (register calling convention: EAX=Self, EDX/ECX args).

## Done

* Full RTTI extraction: **2516 validated tkClass TypeInfos** →
  `research/mappings/classes_full.json` (name, unit, parent, VMT VA, instance
  size, published methods+VAs, published fields+offsets, init-table typed
  fields, owned-class fields).
* Delphi VMT layout for this build fully mapped `[C]` (see
  architecture/overview.md): metadata at ClassType-88…-48, TObject's 11
  virtuals at -44…-4 (Destroy at **-4**), init table where vmtInitTable points.
* Init-table decoding → **private field maps** for 490 classes (2535 typed
  fields) + owned-field (destructor-freed) info for 509 classes `[C]`
  (validated against TFigure's destructor disassembly).
* Ghidra: 2516 VMT labels (`RTTI_<unit>.<Class>`) + 426 published-method
  renames applied; `TFigure.Destroy` verified at 0x93BB58.
* Class reconstructions written up: TFigure, TFigures (see classes/).
* **Record harvest**: 280 tkRecord TypeInfos with full field lists — the
  complete figure model is reconstructed (TSegment 48B with all 16 fields,
  TVertex, TFigVertex, TFigSegment, TCamera, TAttachment) — see
  classes/model-records.md.
* Generated C bindings (include/pivot/pivot_5_2_11.h): 296 structs, 659+
  offset/VA macros, class registry.

## Key addresses (quick reference)

| Thing | VA | Notes |
|-------|----|-------|
| TObject VMT | 0x401760 | parent of everything |
| TMainForm VMT | 0xB14D68 | instance size 0x724, unit MainUnit |
| TMainForm TypeInfo | 0xB1AF40 | |
| TFigure VMT | 0x93AA04 | FigureUnit, size 0x64 |
| TFigure.Destroy | 0x93BB58 | frees fields @+0x34/+0x38 |
| TFigures VMT | 0x946D9C | FiguresUnit, size 0x54 |
| TFigureType VMT | 0xAF97A4 | FigureTypeUnit, size 0x3C |
| TPlayer VMT | 0x950E74 | PlayerUnit, size 0x60 |
| TSprite VMT | 0xAF6A94 | SpriteUnit, size 0x34 |
| TIdxPolygon VMT | 0x92FCA4 | PolygonUnit, size 0x18 |
| TPivotText VMT | 0x937A2C | PivotTextUnit, size 0x28 |
| TWavePlayer VMT | 0xA9CE0C | WavePlay, size 0x44 |
| TWinBitmap VMT | 0x9201B4 | WinBitmap, size 0x8 |
| TThreadedTimer VMT | 0x951918 | ThreadedTimer, size 0x6C |
| TMultiLang VMT | 0xAE4860 | MultiLanguage, size 0x18 |
| TFigureBuilderForm VMT | 0xB02CFC | 88 published methods, 82 fields |

## Open questions / next targets


2. `TMainForm` private state: where the animation frame list lives, undo stack,
   figure type list, canvas control. 146 published fields give the component
   tree; private fields need init-table + decompiler work (in progress).
3. Player/playback loop; figure editing pipeline (drag handlers).
4. File formats: `.piv` (5.x), `.stk` (3.x/4.x/5.x), export paths
   (GIF/AVI/video via FFmpeg units + LibAV DLLs).
5. Menu construction (FMX.Menus TMainMenu on TMainForm — published field
   MainMenu1 @ +0x2F8 per legacy pivotkit).
6. Virtual method slot map for FMX classes (Paint/Resize/MouseDown…) —
   needed for clean vtable hooking of rendering & input.

## Environment notes

* Ghidra 12.1.2 + ghidra-mcp bridge (TCP 8089). Restart Ghidra with
  `GHIDRA_MCP_ALLOW_SCRIPTS=1` to enable `run_script_inline` (already done in
  this workspace). Auto-analysis finds only ~1600 functions — disassemble unit
  code regions on demand (most Delphi code is unreferenced by flow analysis).

## Done (session 3) — 90-100% push

* **pivotlib2** (mods/00_pivotlib2.lua): typed Lua layer over the shipped v1
  runtime using the RE offsets — Frame/Figure objects, tween/background/
  camera read+write (RMW pokes), vertices, figure move, app calls
  (SelectFrame/SetNumFrames/redraw). Functionally tested against mocked
  memory: tests/pivotlib2_test.lua (ALL PASSED).
* **Python execution** (mods/04_python_bridge.lua + tools/pkpython.py):
  `pl2.python.run(code)` returns stdout (verified live: Python 3.13.1);
  `spawn`/`spawn_code` run detached pythonw GUIs that drive Pivot back over
  the bridge. pkpython.Pivot client tested against a simulated bridge
  (tests/pkpython_test.py PASSED). Python mods can build tkinter/PySide
  interfaces and run heavy logic outside the game tick.
* **piv_reader.py**: .piv v5 header VALIDATED across samples (600x600 grey
  / 640x360 sky-blue — width/height/background confirmed `[C]`).
* **Ghidra sweep**: +902 functions discovered in app-unit ranges
  (0x91F000-0x953000, 0xA62000-0xB16000) — total 3531 functions now
  addressable/decompilable.

### Coverage after this session

| Capability | Est. | Basis |
|---|---|---|
| Read/write animation data via Lua | 95% | pivotlib2 tested; remaining: invalidation calls on structural edits |
| Invoke app functionality | 60% | published methods + 3531 discovered functions; naming pass pending |
| Hook events | 55% | published-method hooks proven; deep-hook targets identified |
| Figure types / skeletons | 60% | full model; constructor paths pending |
| Native UI | 30% | design + menu location; FMX construction pending; Python GUIs now available via bridge |
| File formats read | 70% | .piv header validated; .piv body + .stk inner pending |
| Lua+Python ergonomics | 90% | typed Lua + Python exec + bridge client, all tested |

## Done (session 4) - MERGE (e4dd627..efc7fa4)

* One PivotKit, no v1/v2 split: pk_core.c implemented; pk_rtti/pk_hooks/
  pk_api moved into src/ and COMPILED+LINKED into pivotkit.dll.
  Glue: pivot.pk_frame_count -> pk_api (module path live).
* Lua libraries merged: pivotlib = legacy + typed API on one table
  (legacy names win; pl2/pivotlib2 aliases). mods 01-03 unchanged.
* gen_bindings hardened: pack(1), size-correct member types from
  inter-field gaps, union-alias skipping -> offsetof == RE offsets.
* build.bat builds the full merged product (DLL 513KB + loader).
* REGRESSION SUITE (all green on the official build):
  - DLL marker checks: call_addr/hook_addr/bytecode-loader/pk-glue/
    bridge/console/overlay(w16)/sprites(w16) all present
  - pkcompile: 11 mods -> bytecode + 1 python block -> .pyc, 0 failures
  - tests/pivotlib2_test.lua (typed model + UI/events) PASSED
  - tests/merged_lib_test.lua (coexistence, no API loss) PASSED
  - tests/pkpython_test.py (bridge client) PASSED
  - tests/test_pkbindings.c x86 exe: ALL BINDING CHECKS PASSED
  - compiled chain (.lc -> bridge -> .pyc, python 3.13.1) OK
