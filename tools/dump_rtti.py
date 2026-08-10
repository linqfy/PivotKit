#!/usr/bin/env python3
"""Dump published RTTI (methods + fields) from pivot.exe.

Usage:
    python dump_rtti.py [path_to_pivot.exe]

Requires: pefile  (pip install pefile)
"""
import os
import struct
import sys

try:
    import pefile
except ImportError:
    sys.exit("pefile is required:  pip install pefile")

PATH = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.getcwd(), "pivot.exe")

# Anchors from pivot.exe 5.2.11 (also mirrors the constants in src/pivotkit.c)
IMAGEBASE = 0x400000
RTTI_TMAINFORM = 0x00B1AF40   # TypeInfo(TMainForm)
VMT_OFFSET = 12
OFF_VMT_METHODTABLE = 0x34
OFF_VMT_FIELDTABLE = 0x38
OFF_VMT_CLASSNAME = 0x2C

data = open(PATH, "rb").read()
pe = pefile.PE(PATH)

sections = []
for s in pe.sections:
    va0 = IMAGEBASE + s.VirtualAddress
    sections.append((s.Name.rstrip(b"\x00").decode(), va0, s.Misc_VirtualSize,
                     s.PointerToRawData, s.SizeOfRawData))


def va_to_off(va):
    for name, va0, vsz, raw0, rsz in sections:
        if va0 <= va < va0 + max(vsz, rsz):
            return raw0 + (va - va0)
    return None


def u8(va):  return data[va_to_off(va)]
def u16(va): return struct.unpack_from("<H", data, va_to_off(va))[0]
def u32(va): return struct.unpack_from("<I", data, va_to_off(va))[0]


def sstr(va):
    off = va_to_off(va)
    if off is None: return ""
    l = data[off]
    if l > 250 or off + 1 + l > len(data): return ""
    return data[off + 1:off + 1 + l].decode("latin1")


def main():
    ti = RTTI_TMAINFORM
    if u8(ti) != 7:
        sys.exit(f"bad kind at TypeInfo: {u8(ti)}")
    nlen = u8(ti + 1)
    class_type = u32(ti + 2 + nlen)
    vmt_base = class_type - VMT_OFFSET
    print(f"TypeInfo(TMainForm)   @ 0x{ti:X}")
    print(f"ClassType (VMT)       @ 0x{class_type:X}")
    print(f"VMT base              @ 0x{vmt_base:X}")
    print(f"class name            : {sstr(u32(vmt_base - OFF_VMT_CLASSNAME))}")

    mt = u32(vmt_base - OFF_VMT_METHODTABLE)
    off = va_to_off(mt)
    count = u16(mt)
    print(f"\nPublished methods ({count}) @ 0x{mt:X}:")
    p = off + 2
    for i in range(count):
        size = struct.unpack_from("<H", data, p)[0]
        addr = struct.unpack_from("<I", data, p + 2)[0]
        l = data[p + 6]
        nm = data[p + 7:p + 7 + l].decode("latin1", "replace") if l < 120 else "?"
        print(f"  {nm:<38} 0x{addr:08X}")
        p += 7 + l

    ft = u32(vmt_base - OFF_VMT_FIELDTABLE)
    off = va_to_off(ft)
    count = u16(ft)
    print(f"\nPublished fields ({count}) @ 0x{ft:X}:")
    p = off + 2 + 4  # count + parent table pointer
    for i in range(count):
        fo = struct.unpack_from("<I", data, p)[0]
        ni = struct.unpack_from("<H", data, p + 4)[0]
        l = data[p + 6]
        nm = data[p + 7:p + 7 + l].decode("latin1", "replace") if l < 120 else "?"
        print(f"  {nm:<32} offset=0x{fo:04X} nameIndex={ni}")
        p += 7 + l


if __name__ == "__main__":
    main()
