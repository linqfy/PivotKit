# PivotKit RE Knowledge Base — Pivot Animator 5.2.11

> Canonical reverse-engineering knowledge base backing the PivotKit rewrite.
> Target: **pivot.exe 5.2.11 (2025-02-11, Peter Bone / Motusoft GmbH)**,
> Delphi 11 (Alexandria), **FireMonkey (FMX)** UI, DirectX 9 GPU canvas, x86 PE,
> image base `0x400000`, ~11.9 MB image.

Everything here is derived from static analysis of the shipped binary unless a
line says otherwise. Confidence is marked per finding:

* `[C]` confirmed (95–100%) — cross-validated by ≥2 independent signals
* `[S]` strong (80–95%)
* `[H]` hypothesis (<80%)

## Entry points

| Doc | Contents |
|-----|----------|
| [STATUS.md](STATUS.md) | What is done, what remains, next targets |
| [architecture/overview.md](architecture/overview.md) | Binary layout, units, runtime architecture |
| [classes/](classes/) | Per-class reconstruction (fields, methods, behavior) |
| [mappings/classes_full.json](mappings/classes_full.json) | **Machine-readable DB**: all 2516 RTTI classes, VMT addresses, instance sizes, parents, units, published methods (name+VA), published fields (name+offset), init-table typed fields (private fields!), owned-class fields |
| [hooks/hook-points.md](hooks/hook-points.md) | Hook inventory for the runtime |
| [sdk-notes/SDK-DESIGN.md](sdk-notes/SDK-DESIGN.md) | BepInEx-class SDK architecture design |

## Ten-second summary of the platform

* Pivot 5 is a **Delphi 11 FMX** app. All UI is FireMonkey (`FMX.*`), rendered
  through `FMX.Canvas.D2D` / `FMX.Context.DX9` (GPU) with GDI+ fallback.
* The application's own code lives in these units (recovered from RTTI unit
  names, `[C]`):
  `MainUnit, FigureUnit, FiguresUnit, FigureTypeUnit, GlobalTypes, PlayerUnit,
  SpriteUnit, PolygonUnit, PivotTextUnit, WavePlay, WinBitmap, ThreadedTimer,
  MultiLanguage, ZoomImageUnit, ImageSelector, VideoUnit, AVICompression,
  FigureBuilderUnit, ColorUnit, BackgroundColorUnit, LabelBackUnit,
  TrackBarLabelUnit` + FFmpeg integration (`FMX.FFmpeg/FMFDecode/FFEncode`).
* Domain model (see [classes/](classes/) for details):
  * `TFigure` (FigureUnit) — one figure *instance* in one frame
  * `TFigures` (FiguresUnit) — **one animation frame**: figure array +
    background + tween + camera + selection state
  * `TFigureType` (FigureTypeUnit) — the *type* (skeleton) a figure is built from
  * `TPlayer` (PlayerUnit) — playback engine
  * Records in `GlobalTypes`: `TFigVertex, TFigSegment, TCamera, TAttachment,
    TVertex, TSegment…`
* Delphi RTTI gives us published members of forms (e.g. `TMainForm`:
  159 published methods with addresses, 146 published fields) and — via the
  class **init tables** — the *private* field layout of domain classes.

## Tooling

| Tool | Purpose |
|------|---------|
| `tools/dump_rtti_full.py` | Regenerate `classes_full.json` from any pivot.exe |
| `mods/00_pivotlib2.lua` | Typed Lua API (frames/figures/camera) over the v1 runtime |
| `mods/04_python_bridge.lua` | `pl2.python.run/spawn` — Python from Lua (GUIs via pythonw) |
| `tools/pkpython.py` | Python-side SDK: bridge client with typed Frame/Figure refs |
| `tools/piv_reader.py` | .piv/.stk reader (validated header parsing) |
| `tools/gen_ghidra_tsv.py` | Emit Ghidra label/rename ops from the DB |
| Ghidra project (investigation workspace) | `PivotAnim` project; VMT labels + method renames applied |

## Versioning

All VAs in this tree refer to Pivot 5.2.11, SHA-256 of the analyzed binary is
recorded in [STATUS.md](STATUS.md). Use RVAs (`VA - 0x400000`) when comparing
across builds.
