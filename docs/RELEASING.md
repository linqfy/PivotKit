# Releasing

Releases are prereleases because PivotKit is still experimental. A release is
identified by a Git tag, a matching changelog heading, and the archive emitted
by GitHub Actions.

## Before tagging

1. Run the native build and both Lua tests from [BUILDING.md](BUILDING.md).
2. Update `CHANGELOG.md` with the user-visible changes under the next version.
3. Keep the version in the form `vMAJOR.MINOR.PATCH`, for example `v0.5.0`.
4. Confirm the release files are compatible with Pivot 5.2.11 32-bit.

## Publish

Push the tag:

```text
git tag v0.5.0
git push origin v0.5.0
```

The workflow will:

1. build `pivotkit.dll`, `pivotkit-loader.exe`, and the Lua test runner;
2. run the x86 artifact check and both Lua suites;
3. verify that `CHANGELOG.md` contains the tag version;
4. create `pivotkit-v0.5.0.zip` with the binaries, `pivotkit\mods\` assets,
   `tools\pivotctl.py`, license, README, changelog, and documentation;
5. publish a GitHub prerelease using the tag.

The archive mirrors the directory in which users run Pivot:

```text
pivotkit.dll
pivotkit-loader.exe
pivotkit\mods\*.lua
pivotkit\mods\*.jpg
tools\pivotctl.py
README.md
CHANGELOG.md
LICENSE
docs\...
```

Do not include local `bin/` objects, logs, or a copy of `pivot.exe`.

## Commit messages

Use a short, imperative description of the change. Conventional Commit types
make the purpose easy to scan:

```text
docs: reorganize the beginner guides
fix: load mods in filename order
ci: validate release metadata
```

Keep messages specific. Avoid placeholders, internal notes, and emojis.
