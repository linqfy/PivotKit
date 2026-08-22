# File formats — .piv / .stk (5.2.11)

Static analysis of the sample files shipped with Pivot 5.2.11. `[C]` for
container facts (verified by decoding the files), `[H]` for field guesses
pending reader-function decompilation.

## Container

| Format | Container |
|--------|-----------|
| `.piv` (v5 animations) | **raw zlib stream** (`78 DA …`) — no header byte |
| `.stk` (v5 figures) | 1 lead byte (`0x7A` observed) + zlib stream |
| `.stk` legacy (v2/v3) | uncompressed binary (`01 …`) |

Python check: `zlib.decompress(open(f,'rb').read())` for .piv;
`zlib.decompress(data[1:])` for v5 .stk.

## .piv v5 payload (inflated) `[C] container, `[H]` field map

From `Animations/gear wheels.piv` (47182 bytes inflated):

```
05              version = 5                    [C]
58 02 00 00     u32 = 600   (frame width?)     [H]
58 02 00 00     u32 = 600   (frame height?)    [H]
02 00           u16 = 2     (figure type count? frame count?)  [H]
02              u8          (?)
CE CE CE FF     ARGB background color         [H: TAlphaColor]
09 "back grey"  length-prefixed name (named background)  [H]
...             camera floats, figure types, frames
```

Maps to the runtime model: frames are `TFrameSequence` (TArray\<TFigures\>) on
TMainForm+0x654; per-frame background/tween live in TFigures (+0x06..+0x0D);
camera = TFigures+0x38 (TCamera: Pos TPointF, Angle double, Scale single).

## .stk v5 payload (inflated) `[C]` container, `[H]` layout

From cow/dino/football/outline.stk: header begins [u16 id?][3 bytes][data];
the payload contains DOUBLE values including -pi/2 (= 0xBFF921FB54442D18),
matching TSegment.Angle: Double — skeleton segment data per the model.
Inner field map still needs reader decompilation (tools/piv_reader.py
prints raw fields meanwhile).

## Legacy .stk (v2) `[H]`

`01` version byte then uncompressed vertex data (floats like 50.0 observed).
Public documentation of the v2 format exists in the community; cross-check
when decompiling the reader (it branches on the version byte).

## Next steps

* Decompile the readers/writers (entry points: published methods
  `OpenAnimation1…`, `Save1`, `SaveAs1`, figure load/save in FigureTypeUnit)
  to make the field map `[C]`.
* Writer parity is a requirement for the SDK (save-without-loss round trips).
