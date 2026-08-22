# GlobalTypes / model records — complete layouts

All `[C]`: extracted from tkRecord TypeInfos in the binary (init-table field
lists); sizes verified against the RTTI RecSize. Word-sized fields typed as
Integer in RTTI carry base-Integer TypeRefs — true width derived from the next
field's offset.

## TSegment (FigureTypeUnit) — skeleton segment TYPE, 48 bytes
`TI @ 0xAF935C`

| Off | Field | Type |
|----:|-------|------|
| +0x00 | Pivot | Word (vertex index) |
| +0x02 | EndPoint | Word |
| +0x04 | Len | Single |
| +0x08 | Angle | Double |
| +0x10 | Bend | Double |
| +0x18 | Thickness | Single |
| +0x1C | Kind | enum (segment kind) |
| +0x1D | Static | Boolean |
| +0x20 | Color | TColor/TAlphaColor (4) |
| +0x24 | ColorGrad | (4) |
| +0x28 | Transparency | Byte |
| +0x29 | TransparencyGrad | Byte |
| +0x2A | DrawOrderIdx | Integer(2?) |
| +0x2C | ImageIndex | Integer |
| +0x2E | Backwards | Boolean |
| +0x2F | Mirror | Boolean |

## TVertex (FigureTypeUnit) — skeleton vertex TYPE, 12 bytes
`TI @ 0xAF94F0`

| Off | Field | Type |
|----:|-------|------|
| +0x00 | NumAttachments | Integer |
| +0x04 | SegmentsPivot | TArray\<Word\> (dynamic) |
| +0x08 | SegmentsEndPoint | Integer |

## TFigVertex (GlobalTypes) — figure instance vertex, 16 bytes
`TI @ 0xAF5B00`

| Off | Field | Type |
|----:|-------|------|
| +0x00 | Point | TPointF (8: x,y singles) |
| +0x08 | NumAttach | Integer |
| +0x0C | Attach | dynamic array (attachments; owned — freed per-vertex in TFigure.Destroy) |

## TFigSegment (GlobalTypes) — figure instance segment state, 24 bytes
`TI @ 0xAF5B98`

| Off | Field | Type |
|----:|-------|------|
| +0x00 | Angle | Double |
| +0x08 | Bend | Double |
| +0x10 | Length | Single |

(Instance segments only store the *pose* — color/thickness etc. come from the
TSegment type unless the figure overrides them.)

## TCamera (GlobalTypes) — virtual camera, 24 bytes
`TI @ 0x943E00`

| Off | Field | Type |
|----:|-------|------|
| +0x00 | Pos | TPointF |
| +0x08 | Angle | Double |
| +0x10 | Scale | Single |

## TAttachment (GlobalTypes) — figure attachment, 6 bytes
`TI @ 0x93A900`

| Off | Field | Type |
|----:|-------|------|
| +0x00 | Attached | Boolean |
| +0x02 | Figure | Integer (figure index) |
| +0x04 | Vertex | Integer (vertex index) |

## Assembled model

```
TFigureType (0x3C)  = skeleton: vertex list (TVertex[]) + segment list (TSegment[])
TFigure     (0x64)  = instance: TFigVertex[] + TFigSegment[] + type idx + props
TFigures    (0x54)  = frame:    TFigure[] + background + tween + camera
Animation            = sequence of TFigures (owner in MainUnit; TBD)
```

Modding reads/writes at this level are now fully specified statically.
