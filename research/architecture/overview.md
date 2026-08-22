# Architecture — Pivot Animator 5.2.11

## Identity

* PE32 x86, image base `0x400000`, ~11.9 MB, Delphi 11 (Alexandria),
  statically-linked RTL/FMX (no Delphi runtime DLLs).
* UI framework: **FireMonkey (FMX)**; GPU canvas `FMX.Canvas.D2D` +
  `FMX.Context.DX9` (Direct3D 9), GDI+ (`FMX.Canvas.GDIP`) and GPU
  (`FMX.Canvas.GPU`) present as alternatives. `[C]` (unit names from RTTI)
* Video export via LibAV DLLs (avcodec-58 etc.) consumed through custom units
  `FMX.FFmpeg`, `FMX.FFDecode`, `FMX.FFEncode`. `[C]`
* Shell extension `STKPreview.dll` (separate binary, not analyzed yet).

## Application units (recovered from RTTI unit names) `[C]`

| Unit | Key classes | Role |
|------|-------------|------|
| MainUnit | TMainForm | main window: menus, canvas, playback, editing |
| FigureUnit | TFigure | figure instance in a frame |
| FiguresUnit | TFigures | one animation frame (figure list + bg + camera) |
| FigureTypeUnit | TFigureType, records TVertex/TSegment | figure skeleton types |
| GlobalTypes | TFigVertex, TFigSegment, TCamera, TAttachment, arrays | shared model records |
| PlayerUnit | TPlayer | playback |
| SpriteUnit | TSprite | sprite objects |
| PolygonUnit | TIdxPolygon | polyfill polygons |
| PivotTextUnit | TPivotText | text objects |
| WavePlay | TWavePlayer, TQueueThread | audio preview |
| WinBitmap | TWinBitmap | HBITMAP wrapper |
| ThreadedTimer | TThreadedTimer | timer component |
| MultiLanguage | TMultiLang | languages/*.ini loader |
| ZoomImageUnit | TZoomImage | zoomable canvas image |
| ImageSelector | TImageSelector | figure/sprite picker |
| VideoUnit | TVideoForm | export progress UI |
| AVICompression | TAVICompressor | AVI export |
| FigureBuilderUnit | TFigureBuilderForm | figure editor |
| ColorUnit / BackgroundColorUnit | TColorForm, TBackgroundColorForm | color pickers |
| LabelBackUnit / TrackBarLabelUnit | TLabelBack, TTrackBarLabel | UI helpers |

## Delphi VMT layout for this build `[C]`

Offsets from **ClassType** (the value in a class reference / in
`TypeInfo.ClassType`; note pivotkit-legacy used `classType-12` as its base —
identical slots, different origin):

```
-88  vmtSelfPtr       -> ClassType
-84  vmtIntfTable
-80  vmtAutoTable
-76  vmtInitTable     -> init table (see below)
-72  vmtTypeInfo      -> TypeInfo record
-68  vmtFieldTable    -> published fields
-64  vmtMethodTable   -> published methods
-60  vmtDynamicTable
-56  vmtClassName     -> shortstring
-52  vmtInstanceSize
-48  vmtParent        -> parent ClassType
-44  TObject.Equals          }  TObject's 11 virtual
-40  TObject.GetHashCode     }  methods, in declaration
-36  TObject.ToString        }  order; overridden
-32  TObject.SafeCallException } slots hold the
-28  TObject.AfterConstruction } descendant's code
-24  TObject.BeforeDestruction
-20  TObject.Dispatch
-16  TObject.DefaultHandler
-12  TObject.NewInstance
 -8  TObject.FreeInstance
 -4  TObject.Destroy
  0.. (if the class introduces virtuals, their slots; otherwise the
       linker places the init table here for virtual-less classes)
```

Virtual dispatch sites look like `call [eax-4]` (Destroy), `call [eax-0xC]`
(NewInstance), etc. — negative offsets, important for hook planning.

## Class init table (private field map) `[C]`

`vmtInitTable` points at:

```
u32 0x0000000E
u16 0
u16 npairs
u16 0
npairs * [u32 typeRef (class VMT holder)][u32 offset]   ; owned fields
u16 count
u8  pad
count * [u32 typeRef (PPTypeInfo)][u32 offset][u8 len][name][3-byte trailer]
```

The "owned" pairs are fields freed/cleared in the destructor (verified:
TFigure.Destroy frees exactly the +0x34/+0x38 entries). The typed entries give
**name + offset + type** for instance fields including private ones — the
backbone of the SDK bindings.

## TypeInfo (tkClass) `[C]`

```
u8  kind = 7
u8  len
char name[len]          (no alignment padding after the name!)
u32 ClassType
u32 ParentInfo (PPTypeInfo — pointer to pointer)
i16 PropCount
shortstring UnitName
```

## Object instance conventions

* All class instances start with the VMT pointer at +0.
* Managed/dynamic arrays: pointer; length at `ptr-4`; refcount at `ptr-8`.
* UnicodeString: pointer; length at `ptr-4` (chars), codepage/elem size/refcount
  in the 12-byte header before the data.
* Calling convention: Delphi register — EAX=Self, EDX=arg1, ECX=arg2,
  remaining args right-to-left on stack, callee cleans (`ret N`).
