# Documentation

Guides, reverse-engineering knowledge base, and reference for PivotKit
(Pivot Animator 5.2.11).

## Guides

| Guide | Contents |
|-------|----------|
| [Getting started](GETTING_STARTED.md) | Installation, first mod, common fixes |
| [pivotlib guide](PIVOTLIB.md) | The recommended high-level Lua API |
| [Raw Lua API](MOD_API.md) | The lower-level `pivot.*` surface |
| [Architecture](ARCHITECTURE.md) | How the loader, host DLL and mods fit together |
| [Building](BUILDING.md) | Toolchain setup and build commands |
| [Releasing](RELEASING.md) | Packaging a release |

## Reverse-engineering knowledge base

Everything we know about `pivot.exe` 5.2.11 (Delphi 11 / FMX, x86), and how
to reproduce it. Start at [research/INDEX.md](research/INDEX.md); current
state and coverage live in [research/STATUS.md](research/STATUS.md).

| Area | Contents |
|------|----------|
| [research/architecture/](research/architecture/) | Binary layout, VMT structure, RTTI formats |
| [research/classes/](research/classes/) | Reconstructed class layouts (TFigure, TFigures, TMainForm, model records) |
| [research/mappings/](research/mappings/) | `classes_full.json` — the machine-readable symbol database (2,516 classes, private field maps) |
| [research/hooks/](research/hooks/) | Hook-point inventory and constraints |
| [research/files/](research/files/) | `.piv`/`.stk` format analysis |
| [research/sdk-notes/](research/sdk-notes/) | SDK design for the rewrite |

## Ghidra analysis

[ghidra/](ghidra/) packs the analysis state: `functions.tsv` (2,525 functions,
1,113 named), stats, and the procedure to rebuild the Ghidra project from the
binary. See [ghidra/README.md](ghidra/README.md).

## Examples

[examples/](examples/) holds demo mods that are **not** loaded by default.
Copy any of them into `mods/`, compile with `python tools/pkcompile.py`, and
they activate on the next launch. `10_compiled_demo.lua` also demonstrates
the embedded-Python block (`pl2.python.block`).
