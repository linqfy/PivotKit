# TMainForm private state — animation & document model

Extends [TMainForm-surface.md](TMainForm-surface.md) (published members).
Private state recovered from the class init table (owned-pairs section).
`[C]` = type-confirmed via RTTI; `[H]` = inferred.

## Key fields

| Offset | Type (RTTI) | Interpretation | Conf |
|-------:|-------------|----------------|------|
| +0x2F8 | MainMenu1: TMainMenu | published components start here (TForm base = 0x2F8) | `[C]` |
| +0x564 | dynarray, element size 1 (`:TMainForm.:1`) | per-frame byte flags (stored-frame markers?) | `[H]` |
| +0x640 | dynarray, element size 36 (`:TMainForm.:2`) | undo/redo records (36 B each) — or type cache | `[H]` |
| +0x644 | dynarray, element size 36 (`:TMainForm.:3`) | twin of +0x640 (redo stack / second buffer) | `[H]` |
| **+0x654** | **TFrameSequence** (dynarray, element size 4) | **the animation: TArray\<TFigures\>** — one element per frame | `[C]` |
| +0x6C4 | string | current document filename (.piv path) | `[H]` |

## Reading the animation (SDK recipe)

```
mf       = pk_main_form()                       // TMainForm instance
seq      = *(mf + 0x654)                        // dynamic array data ptr
frames   = *(seq - 4)                           // element count (ptr-4)
frame[i] = ((TFigures **)seq)[i]                // TFigures object per frame
```

Per-frame data then reads via the TFigures layout (figures array at +0x10,
tween at +0x0C, background at +0x06, camera record at +0x38). This is exactly
what `src/v2/api.c` implements (`pk_frame_count`, `pk_frame`, …).

## Notes

* `TFrameSequence` is a named dynamic-array type in MainUnit; its element
  type pointer is null in RTTI, but element size 4 + the TFigures frame model
  + usage in SelectFrame/SetNumFrames paths confirm object-reference
  elements. `[C]`
* The twin 36-byte arrays (+0x640/+0x644) need decompiler confirmation —
  prime candidates are the undo/redo stacks (TMainForm has CheckUnstoredFrame,
  undo-related published handlers).
