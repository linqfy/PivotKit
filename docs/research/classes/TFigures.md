# TFigures (FiguresUnit) — one animation frame

VMT `0x946D9C` · instance size `0x54` (84) · parent TObject.

Despite the plural name, **TFigures represents a single frame** of the
animation: the set of figures in that frame plus frame-level state (background,
tweening, camera, selection). The animation as a whole is a sequence of
TFigures (owned by TMainForm state — exact owner TBD). `[S]` (layout `[C]`).

## Layout `[C]`

| Offset | Name | Type | Notes |
|-------:|------|------|-------|
| +0x00 | (VMT) | pointer | |
| +0x04 | FNumFigures | Word | count of used entries in Figures |
| +0x06 | FBackground | Word | background kind (index/color mode) |
| +0x08 | FBackground2 | Word | second background color (gradients) |
| +0x0A | FBackgroundRatio | Byte | gradient ratio |
| +0x0C | FFrameTween | Word | frame inbetweening amount for this frame |
| +0x10 | Figures | dynamic array of TFigure | the frame's figures (owned) |
| +0x14 | DrawOrder | TArrayOfWord | draw z-order (indices into Figures) |
| +0x18 | FrameSelected | Boolean | |
| +0x19 | StartedAttaching | Boolean | |
| +0x1C | EditSelected | TArrayOfWord | indices of figures in edit selection |
| +0x20 | NumSelected | Word | |
| +0x24 | SelectedRect | TRectF (16 B) | selection marquee |
| +0x38 | Camera | TCamera record | virtual camera (Pos, Angle — layout TBD) |

## SDK implications

* Frame = TFigures; FrameCount = length of the animation list in TMainForm.
* Adding a figure programmatically = grow `Figures`, bump `FNumFigures`,
  update `DrawOrder`, then invalidate the canvas (method TBD).
* Background/tween per frame are direct fields — trivially settable once the
  owning form pointer is known.

## Open

* TCamera record layout (GlobalTypes).
* Owner list on TMainForm (private field — find via init table + decompiler).
* Clone/copy-on-new-frame methods in FiguresUnit (code region ~0x946D9C+).
