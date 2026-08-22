# Hook points — Pivot Animator 5.2.11

Inventory of integration points for the PivotKit runtime, from safest to most
invasive. VAs are for 5.2.11 (`2c7911d3…`); everything is `register`
convention (EAX=Self, EDX/ECX args, callee cleans stack args).

## Tier 0 — process-level (already proven in legacy pivotkit)

| Point | Mechanism | Notes |
|-------|-----------|-------|
| `PeekMessageW` IAT slot | IAT patch | per-frame main-thread tick; proven stable |
| `user32!PeekMessageW` (all callers) | inline detour | broader than IAT if needed |
| Main window discovery | EnumWindows | largest visible titled window of PID |

## Tier 1 — RTTI-safe call surface (no addresses needed)

* Any published method of any form class (name→VA via vmtMethodTable):
  `TMainForm` (159), `TFigureBuilderForm` (88), `TTextForm`, `TVideoForm`,
  `TBackgroundColorForm`, `TColorForm`, `TSpriteForm`, `TFrameInsertForm`,
  `TFigureSelectorForm`.
* Published fields via vmtFieldTable (components: MainMenu1 @ +0x2F8 etc.).
* Class references via `find_class_typeinfo(name)` → ClassType (no hardcoded
  VAs; validated chain).

## Tier 2 — fixed-VA function hooks (need the DB)

High-value TMainForm published methods (from research/classes/TMainForm-surface.md):
frame ops (`SelectFrame`, `SetNumFrames`, `SetFrameTween`,
`CheckUnstoredFrame`, `DrawFrameNumber`), playback (`PlayTimerTimer`,
`StopButtonClick`), figure ops (`CreateStickmanType`, `CreateFigureType1Click`,
figure builder launch), file ops (`New1`, `OpenAnimation1`, `Save1`,
`Export1`, `AnimatedGif1` handlers). Hooking these gives semantic events
(frame changed / playback state / document lifecycle) without touching
rendering.

## Tier 3 — virtual method (VMT-slot) hooks

Delphi virtual dispatch reads `[eax + slot]` with **negative** slots for
TObject's 11 virtuals (Destroy = **-4**, NewInstance = -0xC, Dispatch = -0x14,
DefaultHandler = -0x10 …). FMX class-specific virtuals occupy slots after
ClassType+0 interleaved with the init table — per-class slot maps must be
derived before vtable hooking (TODO; needed for Paint/Mouse hooks).

Patching a VMT **slot in the image** affects all instances of that class;
patching per-instance affects one object. Both are viable: the VMT lives in
`.data` (writable, but shared — copy-on-write via VirtualAlloc if needed).

## Tier 4 — deep internal hooks (after further RE)

| Goal | Target | Status |
|------|--------|--------|
| Figure geometry edits | FigureUnit methods over FigVertices/FigSegments arrays | region mapped (0x93B000+), functions TBD |
| Frame list mutation | TMainForm private animation list | owner field TBD |
| Rendering pipeline | FMX TCanvas D2D/DX9 draw calls | TBD |
| File save/load | .piv/.stk readers/writers | TBD |
| Undo/redo | MainUnit undo stack | TBD |

## Hook engine requirements (design constraints from the above)

1. **Register-convention trampolines** — must preserve EAX/EDX/ECX and stack
   args exactly; callee-cleanup `ret N` must be honored when overriding
   returns (legacy pivotkit's stub generator is a working reference).
2. **Hot-patch safety** — 5-byte `jmp rel32` requires ≥5 bytes of prologue;
   Delphi prologues are `push ebp; mov ebp,esp; add esp,-N` — always safe,
   but the stolen bytes must be relocated (length-decoded, not copied blind).
3. **Thread safety** — install on the main thread while the message loop is
   paused (PeekMessage tick makes a natural install window).
4. **Per-class VMT copy** for scoped virtual overrides (avoid global effects).
