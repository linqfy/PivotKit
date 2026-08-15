#!/usr/bin/env python3
"""Dump published RTTI (methods + fields) from pivot.exe.

Usage:
    python dump_rtti.py [path_to_pivot.exe] [--out DIR] [--json] [--markdown]

Without options it prints the classic text listing to stdout.
With --out DIR it also writes:
    DIR/catalog.json   machine-readable catalog (methods + fields)
    DIR/CATALOG.md     markdown catalog grouped by functional area

Requires: pefile  (pip install pefile)
"""
import argparse
import json
import os
import struct
import sys

try:
    import pefile
except ImportError:
    sys.exit("pefile is required:  pip install pefile")

# Anchors from pivot.exe 5.2.11 (also mirrors the constants in src/pivotkit.c)
IMAGEBASE = 0x400000
VMT_OFFSET = 12
OFF_VMT_METHODTABLE = 0x34
OFF_VMT_FIELDTABLE = 0x38
OFF_VMT_CLASSNAME = 0x2C

# ---------------------------------------------------------------------------
# area heuristics - tag each published name with a functional area
# ---------------------------------------------------------------------------

AREAS = [
    ("playback",   ["frame", "play", "stop", "tween", "repeat", "animate"]),
    ("figures",    ["figure", "fig", "join", "flip", "center", "order", "color",
                    "duplicate", "segment", "pose", "sprite", "polygon"]),
    ("canvas",     ["zoom", "cam", "canvas", "align", "showhandles", "handles",
                    "editing", "editmode"]),
    ("ui",         ["label", "menu", "button", "checkbox", "spinedit",
                    "status", "popup", "dialog", "scrollbar", "text"]),
    ("file",       ["save", "open", "load", "export", "import", "undo", "redo",
                    "new", "batch", "svg", "gif", "video", "image", "extention"]),
]


def area_of(name):
    low = name.lower()
    for area, keys in AREAS:
        for k in keys:
            if k in low:
                return area
    return "other"


# ---------------------------------------------------------------------------

def load_pe(path):
    data = open(path, "rb").read()
    pe = pefile.PE(path)
    sections = []
    for s in pe.sections:
        va0 = IMAGEBASE + s.VirtualAddress
        sections.append((s.Name.rstrip(b"\x00").decode(), va0,
                         s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData))
    return data, sections


def main():
    ap = argparse.ArgumentParser(description="Dump Pivot Animator published RTTI")
    ap.add_argument("path", nargs="?", default=os.path.join(os.getcwd(), "pivot.exe"))
    ap.add_argument("--out", default=None, help="output dir for catalog.json / CATALOG.md")
    ap.add_argument("--json", action="store_true", help="also write catalog.json")
    ap.add_argument("--markdown", action="store_true", help="also write CATALOG.md")
    ap.add_argument("--all-classes", action="store_true",
                    help="also scan every class in the image -> classes.json + CLASSES.md")
    args = ap.parse_args()

    if not os.path.exists(args.path):
        sys.exit(f"pivot.exe not found: {args.path}")

    data, sections = load_pe(args.path)


    def va_to_off(va):
        for _name, va0, vsz, raw0, rsz in sections:
            if va0 <= va < va0 + max(vsz, rsz):
                return raw0 + (va - va0)
        return None


    def u8(va):  return data[va_to_off(va)]
    def u16(va): return struct.unpack_from("<H", data, va_to_off(va))[0]
    def u32(va): return struct.unpack_from("<I", data, va_to_off(va))[0]


    def sstr(va):
        off = va_to_off(va)
        if off is None:
            return ""
        l = data[off]
        if l > 250 or off + 1 + l > len(data):
            return ""
        return data[off + 1:off + 1 + l].decode("latin1")

    # find TMainForm TypeInfo dynamically (mirrors init_rtti in pivotkit.c)
    def raw_to_va(raw_off):
        for _name, va0, vsz, raw0, rsz in sections:
            if raw0 <= raw_off < raw0 + rsz:
                return va0 + (raw_off - raw0)
        return None

    def find_class_typeinfo(name):
        name_bytes = name.encode("latin1")
        for i in range(len(data) - 2 - len(name_bytes)):
            if (data[i] == 0x07 and data[i + 1] == len(name_bytes)
                    and data[i + 2:i + 2 + len(name_bytes)] == name_bytes):
                return raw_to_va(i)
        return None

    ti = find_class_typeinfo("TMainForm")
    if ti is None:
        sys.exit("TMainForm TypeInfo not found (wrong pivot.exe build?)")
    if u8(ti) != 7:
        sys.exit(f"bad kind at TypeInfo: {u8(ti)}")

    nlen = u8(ti + 1)
    class_type = u32(ti + 2 + nlen)
    vmt_base = class_type - VMT_OFFSET
    class_name = sstr(u32(vmt_base - OFF_VMT_CLASSNAME))

    methods = []
    mt = u32(vmt_base - OFF_VMT_METHODTABLE)
    off = va_to_off(mt)
    count = u16(mt)
    p = off + 2
    for i in range(count):
        size = struct.unpack_from("<H", data, p)[0]
        addr = struct.unpack_from("<I", data, p + 2)[0]
        l = data[p + 6]
        nm = data[p + 7:p + 7 + l].decode("latin1", "replace") if l < 120 else "?"
        methods.append({"name": nm, "address": f"0x{addr:08X}", "area": area_of(nm)})
        p += 7 + l

    fields = []
    ft = u32(vmt_base - OFF_VMT_FIELDTABLE)
    off = va_to_off(ft)
    count = u16(ft)
    p = off + 2 + 4
    for i in range(count):
        fo = struct.unpack_from("<I", data, p)[0]
        ni = struct.unpack_from("<H", data, p + 4)[0]
        l = data[p + 6]
        nm = data[p + 7:p + 7 + l].decode("latin1", "replace") if l < 120 else "?"
        fields.append({"name": nm, "offset": f"0x{fo:04X}", "area": area_of(nm)})
        p += 7 + l

    info = {
        "class": class_name,
        "typeinfo": f"0x{ti:X}",
        "class_type": f"0x{class_type:X}",
        "vmt_base": f"0x{vmt_base:X}",
        "method_count": len(methods),
        "field_count": len(fields),
    }

    # ------------------------------------------------------------- stdout
    print(f"TypeInfo({class_name})   @ {info['typeinfo']}")
    print(f"ClassType (VMT)          @ {info['class_type']}")
    print(f"VMT base                 @ {info['vmt_base']}")
    print(f"class name               : {class_name}")
    print(f"\nPublished methods ({len(methods)}):")
    for m in methods:
        print(f"  {m['name']:<38} {m['address']}  [{m['area']}]")
    print(f"\nPublished fields ({len(fields)}):")
    for f in fields:
        print(f"  {f['name']:<32} offset={f['offset']}  [{f['area']}]")

    # ------------------------------------------------------------- files
    if args.out:
        out = args.out
        os.makedirs(out, exist_ok=True)
        if args.json:
            with open(os.path.join(out, "catalog.json"), "w", encoding="utf-8") as fh:
                json.dump({"info": info, "methods": methods, "fields": fields},
                          fh, indent=2)
            print(f"\nwrote {os.path.join(out, 'catalog.json')}")
        if args.markdown:
            write_markdown(os.path.join(out, "CATALOG.md"), info, methods, fields)
            print(f"wrote {os.path.join(out, 'CATALOG.md')}")
        if args.all_classes:
            classes = all_classes(data, sections, u8, u16, u32, sstr)
            with open(os.path.join(out, "classes.json"), "w", encoding="utf-8") as fh:
                json.dump({"count": len(classes), "classes": classes}, fh, indent=2)
            write_classes_md(os.path.join(out, "CLASSES.md"), classes)
            print(f"wrote {os.path.join(out, 'classes.json')} ({len(classes)} classes)")
            print(f"wrote {os.path.join(out, 'CLASSES.md')}")


def all_classes(data, sections, u8, u16, u32, sstr):
    """Scan the whole image for tkClass TypeInfo records (kind 7), validating
    each by checking the class name stored at the VMT matches."""
    def va_to_off(va):
        for _n, va0, vsz, raw0, rsz in sections:
            if va0 <= va < va0 + max(vsz, rsz):
                return raw0 + (va - va0)
        return None

    def shortstr_at_ptr(va):
        off = va_to_off(va)
        if off is None or off + 1 >= len(data):
            return None
        ln = data[off]
        if ln < 1 or ln > 250 or off + 1 + ln > len(data):
            return None
        return data[off + 1:off + 1 + ln]

    found = {}   # name -> raw offset of its TypeInfo
    i = 0
    while i < len(data) - 8:
        if data[i] == 0x07:
            ln = data[i + 1]
            if 1 <= ln <= 250 and i + 2 + ln + 4 < len(data):
                nm = data[i + 2:i + 2 + ln].decode("latin1", "replace")
                class_type = struct.unpack_from("<I", data, i + 2 + ln)[0]
                vmt_base = class_type - VMT_OFFSET
                loc = va_to_off(vmt_base - OFF_VMT_CLASSNAME)
                if loc is not None and loc + 4 <= len(data):
                    cn_ptr = struct.unpack_from("<I", data, loc)[0]
                    if shortstr_at_ptr(cn_ptr) == data[i + 2:i + 2 + ln]:
                        if nm not in found:
                            found[nm] = i
                i += 2 + ln + 4
                continue
        i += 1

    classes = []
    for name, ti in sorted(found.items()):
        entry = {"name": name, "methods": [], "fields": []}
        nlen = data[ti + 1]
        class_type = struct.unpack_from("<I", data, ti + 2 + nlen)[0]
        vmt_base = class_type - VMT_OFFSET

        mt_loc = va_to_off(vmt_base - OFF_VMT_METHODTABLE)
        if mt_loc is not None and mt_loc + 2 <= len(data):
            mt_off = va_to_off(struct.unpack_from("<I", data, mt_loc)[0])
            if mt_off is not None:
                mcount = struct.unpack_from("<H", data, mt_off)[0]
                p = mt_off + 2
                for _ in range(mcount):
                    if p + 7 > len(data):
                        break
                    l = data[p + 6]
                    entry["methods"].append(
                        data[p + 7:p + 7 + l].decode("latin1", "replace") if 0 < l < 120 else "?")
                    p += 7 + l

        ft_loc = va_to_off(vmt_base - OFF_VMT_FIELDTABLE)
        if ft_loc is not None and ft_loc + 2 <= len(data):
            ft_off = va_to_off(struct.unpack_from("<I", data, ft_loc)[0])
            if ft_off is not None:
                fcount = struct.unpack_from("<H", data, ft_off)[0]
                p = ft_off + 2 + 4
                for _ in range(fcount):
                    if p + 7 > len(data):
                        break
                    l = data[p + 6]
                    entry["fields"].append(
                        data[p + 7:p + 7 + l].decode("latin1", "replace") if 0 < l < 120 else "?")
                    p += 7 + l
        classes.append(entry)
    return classes


def write_classes_md(path, classes):
    classes = sorted(classes, key=lambda c: c["name"].lower())
    lines = [
        "# Pivot Animator 5.2.11 — all published classes",
        "",
        f"**{len(classes)}** classes found in the image (tkClass RTTI, validated "
        "against each VMT's class name). Published counts are per-class own tables.",
        "",
        "> Use `pivot.enum_classes()` in Lua to get the same list at runtime, and "
        "`pivotlib.scan('TClassName')` to find live instances.",
        "",
        "| Class | Methods | Fields |",
        "|-------|--------:|-------:|",
    ]
    for c in classes:
        lines.append(f"| `{c['name']}` | {len(c['methods'])} | {len(c['fields'])} |")
    lines.append("")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


def write_markdown(path, info, methods, fields):
    AREA_TITLES = [
        ("playback", "Playback & frames"),
        ("figures", "Figures & selection"),
        ("canvas", "Canvas / zoom / camera"),
        ("ui", "Status bar & UI"),
        ("file", "File & undo"),
        ("other", "Other"),
    ]
    lines = []
    lines.append(f"# Pivot Animator 5.2.11 — published RTTI catalog")
    lines.append("")
    lines.append(f"Class **{info['class']}** — generated by `tools/dump_rtti.py` "
                 f"from `pivot.exe` (TypeInfo @ {info['typeinfo']}).")
    lines.append("")
    lines.append(f"- **{info['method_count']}** published methods")
    lines.append(f"- **{info['field_count']}** published fields")
    lines.append("")
    lines.append("Names are grouped by *functional area* using a name heuristic — "
                 "the grouping is a guide, not a guarantee.")
    lines.append("")
    lines.append("> All of these are reachable from Lua through the `pivotlib` proxy "
                 "objects (e.g. `form:SetFrameNumber(3)`, `form.PlayButton`). "
                 "See [PIVOTLIB.md](PIVOTLIB.md).")
    lines.append("")
    for area, title in AREA_TITLES:
        ms = [m for m in methods if m["area"] == area]
        fs = [f for f in fields if f["area"] == area]
        lines.append(f"## {title}")
        lines.append("")
        if ms:
            lines.append("| Method | Address |")
            lines.append("|--------|---------|")
            for m in sorted(ms, key=lambda x: x["name"].lower()):
                lines.append(f"| `{m['name']}` | `{m['address']}` |")
            lines.append("")
        if fs:
            lines.append("| Field | Offset |")
            lines.append("|-------|--------|")
            for f in sorted(fs, key=lambda x: x["name"].lower()):
                lines.append(f"| `{f['name']}` | `{f['offset']}` |")
            lines.append("")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    main()
