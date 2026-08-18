# Architecture

## Structural decision

PivotKit keeps one native injection boundary and several clearly named public
surfaces:

```text
pivot.exe
  │
  ├─ pivotkit-loader.exe  starts Pivot suspended and injects the DLL
  └─ pivotkit.dll          owns RTTI, hooks, the Lua state, and main-thread work
       └─ pivot.*           low-level Lua bridge
            └─ pivotlib     maintained high-level Lua library in mods/
                 └─ example mods and user mods
```

The repository follows the same boundary:

- `src/` is compatibility-sensitive native code.
- `mods/00_pivotlib.lua` is the high-level mod API and loads before examples.
- `mods/` contains shipped examples, not hidden application code.
- `tests/` validates the Lua layer with a deterministic mock.
- `tools/` contains offline utilities and the bridge client.
- `docs/` contains maintained guides; `docs/reference/` contains generated RTTI.

The native file is intentionally kept as one translation unit for now. It
shares ABI-sensitive state across RTTI, hooks, the message-loop tick, and Lua
registration. Splitting it is a future maintenance project only if each
boundary gets an explicit interface and a live Pivot smoke test.

## Runtime rules

1. Pivot calls and Lua callbacks run on Pivot's main thread.
2. The `PeekMessageW` IAT hook supplies the per-frame tick.
3. Input events are polled; they do not inline-hook Pivot's FMX input methods.
4. Published RTTI is the supported reflection surface. Private offsets are
   diagnostic and build-specific.
5. The 5.2.11 x86 layout is a compatibility contract, not a generic parser.

## Documentation rules

- Put beginner instructions in `README.md` and `docs/GETTING_STARTED.md`.
- Put API behavior in `docs/PIVOTLIB.md` and `docs/MOD_API.md`.
- Put generated output only in `docs/reference/`.
- Update `CHANGELOG.md` for user-visible changes and `RELEASING.md` for the
  package contract.
