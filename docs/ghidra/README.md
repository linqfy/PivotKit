# Ghidra analysis — Pivot Animator 5.2.11

Reverse-engineering artifacts for `pivot.exe` 5.2.11
(SHA-256 `2c7911d3303cc28d…`, image base `0x400000`, compiler `borlanddelphi`).

## Contents

| File | What it is |
|------|------------|
| `functions.tsv` | Every discovered function: entry VA, name, body size (2,525 rows; 1,113 carry names — `RTTI_<Unit>.<Class>` labels and `Unit.Class.method` renames from the RTTI database) |
| `analysis_stats.tsv` | Counts: functions, symbols, memory blocks |

## What's in the labeled database

* 2,516 class VMTs labeled `RTTI_<unit>.<Class>` (from `docs/research/mappings/classes_full.json`)
* 426 published methods renamed `Unit.Class.method`
* ~900 additional functions discovered by prologue sweep (`55 8B EC`) in the app-unit code ranges `0x91F000–0x953000` and `0xA62000–0xB16000`
* `TFigure.Destroy` verified at `0x93BB58` (VMT slot −4)

## Reproducing the project

The binary is not stored in-repo. To rebuild the analysis:

1. Import `pivot.exe` (5.2.11, hash above) into a Ghidra 12.x project — accept the `borlanddelphi` compiler guess (register convention: EAX=Self, EDX/ECX args).
2. Generate the label TSV: `python tools/dump_rtti_full.py <pivot.exe>` then `python tools/gen_ghidra_tsv.py`.
3. Apply it (Ghidra script or the ghidra-mcp `run_script_inline`): `VMT <hex-va> <name>` labels, `MTD <hex-va> <name>` function renames — 2,941 ops, source: `docs/research/mappings/ghidra_rtti_labels.tsv`.
4. Prologue-sweep the two app-unit ranges above and create functions at each `55 8B EC`.

Auto-analysis alone finds ~1,600 functions; the sweep and RTTI labels account for the rest.
