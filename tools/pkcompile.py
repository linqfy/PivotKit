#!/usr/bin/env python3
"""pkcompile.py - compile PivotKit mods: no runtime interpretation.

1. Lua mods  (mods/*.lua)      -> mods/compiled/<name>.lc  (Lua 5.4 bytecode,
                                 produced with the bundled lua.exe via
                                 string.dump, so bytecode matches the
                                 runtime interpreter exactly)
2. Python blocks inside mods   -> pymods/<mod>__<block>.py + __pycache__/*.pyc
                                 (py_compile'd; runs on the full system
                                 CPython, so ANY installed library works)

Python block syntax inside a .lua mod (a Lua long comment containing raw
multiline Python; the body must not contain "]]"):

    --[[ @python myblock
    import math, json          # any stdlib or pip-installed library
    ...
    -- @end ]]

At runtime call pl2.python.block("myblock") (mods/04_python_bridge.lua) -
it executes the compiled bytecode via the system python.

Usage:
    python tools/pkcompile.py [--mods DIR] [--out DIR] [--pyout DIR]
                              [--luac PATH_TO_LUA_EXE] [--keep-source]
"""
import argparse
import os
import py_compile
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))

PY_BEGIN = re.compile(r"^\s*--\[\[\s*@python\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
PY_END = re.compile(r"^\s*--\s*@end\s*\]\]")


def compile_lua(luac, src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    code = (
        "local f=assert(loadfile([[%s]])); "
        "local o=assert(io.open([[%s]],'wb')); "
        "o:write(string.dump(f)); o:close(); print('ok')"
        % (src, dst)
    )
    r = subprocess.run([luac, "-e", code], capture_output=True, text=True)
    if r.returncode != 0 or "ok" not in r.stdout:
        return False, (r.stderr or r.stdout).strip()
    # sanity: the bytecode must load
    v = subprocess.run([luac, "-e", "assert(loadfile([[%s]]))" % dst],
                       capture_output=True, text=True)
    return v.returncode == 0, (v.stderr or "").strip()


def extract_python(src_path, pyout):
    blocks = {}
    name = None
    body = []
    for line in open(src_path, encoding="utf-8", errors="replace"):
        if name is None:
            m = PY_BEGIN.match(line)
            if m:
                name = m.group(1)
                body = []
        else:
            if PY_END.match(line):
                mod = os.path.splitext(os.path.basename(src_path))[0]
                blocks[f"{mod}__{name}"] = "".join(body)
                name = None
            else:
                body.append(line)
    written = []
    for bname, code in blocks.items():
        path = os.path.join(pyout, bname + ".py")
        os.makedirs(pyout, exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(code)
        py_compile.compile(path, doraise=True)   # -> __pycache__/*.pyc
        written.append(bname)
    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mods", default=os.path.join(ROOT, "mods"))
    ap.add_argument("--out", default=None, help="compiled Lua dir")
    ap.add_argument("--pyout", default=os.path.join(ROOT, "pymods"))
    ap.add_argument("--luac", default=os.path.join(ROOT, "bin", "lua.exe"))
    args = ap.parse_args()
    out = args.out or os.path.join(args.mods, "compiled")

    if not os.path.isdir(args.mods):
        sys.exit(f"mods dir not found: {args.mods}")

    ok = fail = 0
    py_blocks = []
    for fn in sorted(os.listdir(args.mods)):
        if not fn.endswith(".lua"):
            continue
        src = os.path.join(args.mods, fn)
        dst = os.path.join(out, fn[:-4] + ".lc")
        good, err = compile_lua(args.luac, src, dst)
        if good:
            ok += 1
            print(f"[lc] {fn}")
        else:
            fail += 1
            print(f"[FAIL] {fn}: {err}")
        try:
            py_blocks += extract_python(src, args.pyout)
        except py_compile.PyCompileError as e:
            fail += 1
            print(f"[FAIL-python] {fn}: {e}")
    for b in py_blocks:
        print(f"[pyc] {b}")

    print(f"\ncompiled {ok} lua mod(s), {len(py_blocks)} python block(s); "
          f"{fail} failure(s)")
    sys.exit(1 if fail else 0)


if __name__ == "__main__":
    main()
