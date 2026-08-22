#!/usr/bin/env python3
"""Full static RTTI dump for Pivot Animator 5.2.11 (Delphi 11 FMX, x86).

Extracts every validated tkClass TypeInfo record from the PE image and emits
a machine-readable JSON knowledge base:

  * class name, TypeInfo VA, ClassType/VMT VA
  * instance size, parent class, unit name
  * published methods (name + VA)
  * published fields (name + instance offset)

Delphi x86 VMT layout used here (offsets from the ClassType pointer,
i.e. the value stored in a class reference / in TypeInfo.ClassType):

    vmtSelfPtr      = ClassType - 88   (0xFFFFFFA8) -> points to vmtSelfPtr addr
    vmtIntfTable    = -84
    vmtAutoTable    = -80
    vmtInitTable    = -76
    vmtTypeInfo     = -72              -> points at the TypeInfo record
    vmtFieldTable   = -68
    vmtMethodTable  = -64
    vmtDynamicTable = -60
    vmtClassName    = -56              -> shortstring ptr
    vmtInstanceSize = -52
    vmtParent       = -48              -> parent ClassType ptr

Note: pivotkit.cpp computes vmtBase = ClassType - 12 and then reads
vmtBase-0x34 (=-64), vmtBase-0x38 (=-68), vmtBase-0x2C (=-56),
vmtBase-0x28 (=-52) - identical slots, different origin.

TTypeData for tkClass (Delphi 10.x/11, after 4-byte alignment):
    ClassType: TClass            (4)
    ParentInfo: ^PPTypeInfo      (4)  -> parent's TypeInfo VA
    PropCount: SmallInt          (2)
    UnitName: ShortString        (1+len)
    (4-aligned) PropData: PropCount: Word + TPropInfo entries

Usage:
    python dump_rtti_full.py [pivot.exe] [--out classes_full.json]
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

IMAGEBASE = 0x400000

VMT_INITTABLE = -76
VMT_TYPEINFO = -72
VMT_FIELDTABLE = -68
VMT_METHODTABLE = -64
VMT_DYNAMICTABLE = -60
VMT_CLASSNAME = -56
VMT_INSTANCESIZE = -52
VMT_PARENT = -48


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        pe = pefile.PE(path)
        self.sections = []
        for s in pe.sections:
            self.sections.append(
                (s.Name.rstrip(b"\x00").decode("latin1"),
                 IMAGEBASE + s.VirtualAddress,
                 s.Misc_VirtualSize,
                 s.PointerToRawData,
                 s.SizeOfRawData))
        self.image_size = pe.OPTIONAL_HEADER.SizeOfImage

    def va_to_off(self, va):
        for _n, va0, vsz, raw0, rsz in self.sections:
            if va0 <= va < va0 + max(vsz, rsz):
                return raw0 + (va - va0)
        return None

    def off_to_va(self, off):
        for _n, va0, vsz, raw0, rsz in self.sections:
            if raw0 <= off < raw0 + rsz:
                return va0 + (off - raw0)
        return None

    def u8(self, va):
        o = self.va_to_off(va)
        return None if o is None else self.data[o]

    def u16(self, va):
        o = self.va_to_off(va)
        return None if o is None else struct.unpack_from("<H", self.data, o)[0]

    def u32(self, va):
        o = self.va_to_off(va)
        return None if o is None else struct.unpack_from("<I", self.data, o)[0]

    def sstr(self, va, maxlen=250):
        """Delphi shortstring at VA -> str (or None)."""
        o = self.va_to_off(va)
        if o is None or o >= len(self.data):
            return None
        ln = self.data[o]
        if ln > maxlen or o + 1 + ln > len(self.data):
            return None
        return self.data[o + 1:o + 1 + ln].decode("latin1", "replace")

    def in_image(self, va):
        return IMAGEBASE <= va < IMAGEBASE + self.image_size


def scan_typeinfos(img):
    """Find candidate tkClass TypeInfos: kind=7, len, name, ClassType in image,
    vmtClassName roundtrip matches. Returns {name: typeinfo_va}."""
    found = {}
    data = img.data
    i = 0
    n = len(data)
    while i < n - 8:
        if data[i] != 0x07:
            i += 1
            continue
        ln = data[i + 1]
        if not (1 <= ln <= 250) or i + 2 + ln + 4 > n:
            i += 1
            continue
        name = data[i + 2:i + 2 + ln].decode("latin1", "replace")
        # TypeData is 4-aligned after kind+namelen+name
        ti_va = img.off_to_va(i)
        if ti_va is None:
            i += 2 + ln + 4
            continue
        class_type = struct.unpack_from("<I", data, i + 2 + ln)[0]
        if not img.in_image(class_type):
            i += 1
            continue
        cn = img.u32(class_type + VMT_CLASSNAME)
        if cn and img.sstr(cn) == name:
            if name not in found:
                found[name] = ti_va
        i += 1  # step by 1: records are byte-packed; validated by roundtrip
    return found


def parse_init_table(img, class_type, inst_size):
    """Parse the class init table (vmtInitTable @ ClassType-76).

    Empirical format for this binary (validated on TFigure/TFigures):
      +0x00: u32 0x0000000E
      +0x04: u16 0
      +0x06: u16 npairs
      +0x08: u16 0 (pad)
      +0x0A: npairs x [u32 typeVa (class VMT)][u32 offset]   (owned fields)
      then:  pad zeros, u16 count, 1 pad byte,
      then:  count x [u32 typeVa (TypeInfo)][u32 offset][u8 len][name][3B trailer]
    Returns (owned_pairs, typed_fields); robust to prologue drift.
    """
    owned, fields = [], []
    it = img.u32(class_type + VMT_INITTABLE)
    if not it or not img.in_image(it) or img.u32(it) != 0x0E:
        return owned, fields
    npairs = img.u16(it + 6)
    p = it + 10
    if npairs > 256:
        npairs = 0
    for _ in range(npairs):
        tva = img.u32(p)
        off = img.u32(p + 4)
        if tva and img.in_image(tva) and off < max(inst_size or 1, 1):
            owned.append({"type_va": tva, "offset": off})
        p += 8
    # Scan for the typed-field entry list: a u16 count followed (3 bytes
    # later) by entries that parse cleanly.
    for q in range(p, min(p + 40, p + 40)):
        cnt = img.u16(q)
        if not (0 < cnt <= 512):
            continue
        for start in (q + 3, q + 2):
            got = []
            pp = start
            ok = True
            for _ in range(cnt):
                tva = img.u32(pp)
                off = img.u32(pp + 4)
                ln = img.u8(pp + 8)
                if ln is None or ln < 1 or ln > 100:
                    ok = False
                    break
                nm = img.sstr(pp + 8)
                if nm is None or not nm.isprintable() or off >= max(inst_size or 1 << 20, 1 << 16):
                    ok = False
                    break
                if tva and not img.in_image(tva):
                    ok = False
                    break
                got.append({"name": nm, "offset": off, "type_va": tva})
                pp += 9 + ln + 3
            if ok and len(got) == cnt:
                return owned, got
    return owned, fields


TYPENAME_CACHE = {}


def parse_record_fields(img, ti_va, recsize):
    """Parse a tkRecord TypeInfo's field list.

    Format (validated on TCamera/TAttachment/TSegment in this binary):
      [u8 kind=14][u8 len][name][u32 recSize][10 bytes prologue]
      entries: [u32 typeVa][u32 offset][u8 0x02][u8 len][name][u8 0x02][u8 0x00]
    Returns list of {name, offset, type_va}.
    """
    out = []
    ln = img.u8(ti_va + 1)
    base = ti_va + 2 + ln + 4  # end of recSize
    p = None
    for probe in range(base + 6, base + 40):
        if img.u8(probe + 8) == 0x02:
            l = img.u8(probe + 9)
            if l and 1 <= l <= 64:
                nm = img.sstr(probe + 9)
                if nm and nm.isprintable() and len(nm) == l \
                        and img.u8(probe + 10 + l) == 0x02:
                    off0 = img.u32(probe + 4)
                    if off0 is not None and off0 <= 1:
                        p = probe
                        break
    if p is None:
        return []
    for _ in range(64):
        tva = img.u32(p)
        off = img.u32(p + 4)
        if tva is None or off is None or off >= max(recsize, 1):
            break
        if img.u8(p + 8) != 0x02:
            break
        l = img.u8(p + 9)
        if l is None or not (1 <= l <= 64):
            break
        nm = img.sstr(p + 9)
        if nm is None or not nm.isprintable() or len(nm) != l:
            break
        if img.u8(p + 10 + l) != 0x02:
            break
        out.append({"name": nm, "offset": off, "type_va": tva})
        p += 4 + 4 + 1 + 1 + l + 2
    return out


def scan_records(img, max_records=4000):
    """Find tkRecord TypeInfos in the image and parse their field lists."""
    data = img.data
    recs = {}
    i = 0
    n = len(data)
    while i < n - 8 and len(recs) < max_records:
        if data[i] == 0x0E:
            ln = data[i + 1]
            if 1 <= ln <= 250 and i + 2 + ln + 4 <= n:
                name = data[i + 2:i + 2 + ln].decode("latin1", "replace")
                recsize = struct.unpack_from("<I", data, i + 2 + ln)[0]
                if 0 < recsize <= 0x10000 and name.isidentifier():
                    ti_va = img.off_to_va(i)
                    if ti_va is not None and name not in recs:
                        fields = parse_record_fields(img, ti_va, recsize)
                        if fields:
                            recs[name] = {
                                "name": name, "recsize": recsize,
                                "typeinfo_va": ti_va, "fields": fields,
                            }
        i += 1
    return recs


def deref_typeinfo(img, tva):
    """Type refs in init tables are PPTypeInfo (pointer to pointer).
    Deref until we land on something that looks like a TypeInfo record."""
    for _ in range(3):
        if not tva or not img.in_image(tva):
            return None
        k = img.u8(tva)
        if 0 < k <= 0x16:  # plausible TTypeKind
            nm = img.sstr(tva + 1)
            if nm and len(nm) >= 1 and all(32 <= ord(ch) < 127 for ch in nm):
                return tva
        nxt = img.u32(tva)
        if not nxt or nxt == tva:
            return None
        tva = nxt
    return None


def resolve_type(img, tva):
    """Best-effort: turn a type ref into a human type string."""
    ti = deref_typeinfo(img, tva)
    if ti is None:
        return None
    if ti in TYPENAME_CACHE:
        return TYPENAME_CACHE[ti]
    kind = img.u8(ti)
    name = img.sstr(ti + 1) or "?"
    td = ti + 2 + len(name)
    out = name
    if kind == 1:
        out = "Integer"
    elif kind == 4:
        out = f"Float:{name}"
    elif kind == 2:
        out = "Char"
    elif kind == 0x11:  # tkDynArray: TypeData = elSize?/elType PPTypeInfo...
        elptr = img.u32(td + 4)
        el = deref_typeinfo(img, elptr) if elptr else None
        if el:
            elname = img.sstr(el + 1) or "?"
            out = f"TArray<{elname}>"
        else:
            out = f"DynArray:{name}"
    elif kind == 14:  # tkRecord
        out = f"record {name}"
    elif kind == 7:
        out = f"class {name}"
    TYPENAME_CACHE[ti] = out
    return out




def parse_typedata(img, ti_va, name):
    """Parse the tkClass TTypeData following kind+name at ti_va.

    Empirically confirmed against this binary (see TMainForm @ 0xB1AF40):
    there is NO alignment padding - ClassType sits at ti+2+len directly.
    """
    ln = img.u8(ti_va + 1)
    td = ti_va + 2 + ln
    class_type = img.u32(td)
    parent_ti = img.u32(td + 4)
    prop_count = img.u16(td + 8)
    unit = img.sstr(td + 10)
    return class_type, parent_ti, prop_count, unit


def parse_methods(img, class_type):
    out = []
    mt = img.u32(class_type + VMT_METHODTABLE)
    if not mt or not img.in_image(mt):
        return out
    o = img.va_to_off(mt)
    if o is None:
        return out
    count = struct.unpack_from("<H", img.data, o)[0]
    if count > 4096:
        return out
    p = o + 2
    for _ in range(count):
        if p + 7 > len(img.data):
            break
        addr = struct.unpack_from("<I", img.data, p + 2)[0]
        l = img.data[p + 6]
        nm = img.data[p + 7:p + 7 + l].decode("latin1", "replace")
        out.append({"name": nm, "va": addr})
        p += 7 + l
    return out


def parse_fields(img, class_type):
    out = []
    ft = img.u32(class_type + VMT_FIELDTABLE)
    if not ft or not img.in_image(ft):
        return out
    o = img.va_to_off(ft)
    if o is None:
        return out
    count = struct.unpack_from("<H", img.data, o)[0]
    if count > 4096:
        return out
    p = o + 2 + 4  # skip count + parent table ptr
    for _ in range(count):
        if p + 7 > len(img.data):
            break
        fo = struct.unpack_from("<I", img.data, p)[0]
        token = struct.unpack_from("<H", img.data, p + 4)[0]
        l = img.data[p + 6]
        nm = img.data[p + 7:p + 7 + l].decode("latin1", "replace")
        out.append({"name": nm, "offset": fo, "token": token})
        p += 7 + l
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="?", default=os.path.join(
        os.path.dirname(__file__), "..", "..", "pivot.exe"))
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    img = Image(args.path)
    tis = scan_typeinfos(img)
    print(f"[+] {len(tis)} validated tkClass TypeInfos")

    # map typeinfo_va -> name for parent resolution
    by_va = {va: nm for nm, va in tis.items()}

    classes = []
    for name, ti_va in sorted(tis.items()):
        class_type, parent_ti, prop_count, unit = parse_typedata(
            img, ti_va, name)
        if class_type is None:
            continue
        inst_size = img.u32(class_type + VMT_INSTANCESIZE)
        # ParentInfo is a PPTypeInfo: pointer-to-pointer. Dereference once
        # more to reach the parent's TypeInfo record (confirmed: TFigure's
        # ParentInfo 0x401FD8 -> [0x401FDC] -> 07 07 'TObject').
        parent_name = None
        if parent_ti and img.in_image(parent_ti):
            pti = img.u32(parent_ti)
            if pti and img.in_image(pti) and img.u8(pti) == 7:
                parent_name = img.sstr(pti + 1)
        rec = {
            "name": name,
            "unit": unit,
            "typeinfo_va": ti_va,
            "classtype_va": class_type,
            "instance_size": inst_size,
            "parent": parent_name,
            "parent_typeinfo_va": parent_ti,
            "published_methods": parse_methods(img, class_type),
            "published_fields": parse_fields(img, class_type),
        }
        owned, tfields = parse_init_table(img, class_type, inst_size)
        if owned:
            rec["owned_fields"] = owned
        if tfields:
            rec["typed_fields"] = tfields
        classes.append(rec)

    # Resolution pass: owned_fields type_va are class VMTs; typed_fields
    # type_va are TypeInfos. Turn both into readable names.
    vmt_to_name = {c["classtype_va"]: c["name"] for c in classes if c["classtype_va"]}
    ti_to_name = {c["typeinfo_va"]: c["name"] for c in classes if c["typeinfo_va"]}
    for c in classes:
        for o in c.get("owned_fields", []):
            # owned pair type_va points at a global holding a class VMT
            v = img.u32(o.get("type_va")) if o.get("type_va") else None
            nm = vmt_to_name.get(v)
            if nm is None and o.get("type_va") in vmt_to_name:
                nm = vmt_to_name[o["type_va"]]
            o["type"] = nm
        for f in c.get("typed_fields", []):
            tv = f.get("type_va")
            if tv in ti_to_name:
                f["type"] = ti_to_name[tv]
            else:
                f["type"] = resolve_type(img, tv)

    # Record harvest: tkRecord TypeInfos with field lists
    records = scan_records(img)
    for r in records.values():
        for f in r["fields"]:
            tv = f.get("type_va")
            if tv in ti_to_name:
                f["type"] = ti_to_name[tv]
            else:
                f["type"] = resolve_type(img, tv)
    print(f"[+] {len(records)} records with field lists")

    out = args.out or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "..", "docs", "research", "mappings", "classes_full.json")
    out = os.path.abspath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8") as fh:
        json.dump({"count": len(classes), "classes": classes,
                   "records": sorted(records.values(),
                                     key=lambda r: r["name"].lower())}, fh, indent=1)
    print(f"[+] wrote {out}")

    # Summary of app-owned (non-library-unit) classes
    app_units = {}
    for c in classes:
        u = c.get("unit") or "?"
        app_units.setdefault(u, 0)
        app_units[u] += 1
    print("[+] units with class counts:")
    for u, n in sorted(app_units.items(), key=lambda kv: -kv[1])[:60]:
        print(f"    {u:<40} {n}")


if __name__ == "__main__":
    main()
