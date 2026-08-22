# TFigure (FigureUnit) — figure instance

VMT `0x93AA04` · TypeInfo `0x93B934` · instance size `0x64` (100) · parent TObject.

One TFigure = one figure placed in one frame. Geometry lives in two dynamic
arrays (vertices + segments); the skeleton it was built from is referenced by
`FFigureType` index. `[C]` — layout from the class init table, cross-validated
by TFigure.Destroy (0x93BB58) which frees exactly +0x34 and +0x38.

## Layout `[C]`

| Offset | Name | Type | Notes |
|-------:|------|------|-------|
| +0x00 | (VMT) | pointer | |
| +0x04 | FExtent | TRect (16 B) | integer bounding box |
| +0x14 | FExtentF | TRectF (16 B) | float bounding box (subpixel) |
| +0x24 | FColor | TAlphaColor | ARGB |
| +0x28 | FTrans | Byte | transparency 0..255 |
| +0x2C | FScale | Single | figure scale |
| +0x30 | FFigureType | Word | index into the figure-type list |
| +0x34 | FigVertices | TArray\<TFigVertex\> | dynamic array (owned; freed in Destroy; header 12 B, 8-byte stride observed in Destroy loop) |
| +0x38 | FigSegments | TFigSegments (dyn array) | dynamic array (owned) |
| +0x3C | Flipped | Boolean | |
| +0x3E | NumSegVarLen | Word | variable-length segment count |
| +0x40 | NumSegVarBend | Word | bendy segment count |
| +0x42 | Attachment | TAttachment record (6 B) | attach-to-figure state |
| +0x48 | NumSegAttach | Word | |
| +0x4A | CanAttach | Boolean | |
| +0x4C | DrawOrderIdx | Word | z-order |
| +0x50 | Selected | Integer | selection state |
| +0x54 | InSelectBox | Boolean | |
| +0x55 | Moved | Boolean | |
| +0x58 | ID | Integer | figure id |
| +0x5C | Next | Word | linked-list next (frame copy chain?) |
| +0x5E | Prev | Word | linked-list prev |

## Destroy (0x93BB58) behavior `[C]`

1. Reads count from `FigVertices` (`*(ptr-4)`), iterates elements
   (`base + 8*i + 0xC`), frees each vertex's owned data.
2. Clears `FigVertices` (+0x34) and `FigSegments` (+0x38).
3. Calls inherited cleanup conditionally on a flag captured at entry
   (standard Delphi two-phase destroy).

## Open

* TFigVertex / TFigSegment record layouts (GlobalTypes) — needed for real
  figure editing.
* Methods beyond Destroy: FigureUnit code region 0x93B000–0x946000 holds
  constructors/clone/draw/serialize — map via decompiler.
