#!/usr/bin/env python3
"""piv_reader.py - read Pivot Animator files (5.x .piv animations, .stk figures).

Container (verified against the samples shipped with 5.2.11):
  .piv        raw zlib stream
  .stk (v5)   1 lead byte + zlib stream
  .stk (old)  uncompressed binary (version byte 1..4)

Usage:
  python tools/piv_reader.py ANIMATION.piv --summary
  python tools/piv_reader.py FIGURE.stk --summary --hex 64
"""
import argparse
import struct
import sys
import zlib


def load(path):
    data = open(path, "rb").read()
    if data[:2] == b"\x78\x9c" or data[:2] == b"\x78\xda" or data[:2] == b"\x78\x01":
        return zlib.decompress(data), "zlib"
    if data[1:3] in (b"\x78\x9c", b"\x78\xda", b"\x78\x01"):
        return zlib.decompress(data[1:]), "lead+zlib"
    return data, "raw"


class R:
    def __init__(self, b):
        self.b, self.o = b, 0

    def u8(self):
        v = self.b[self.o]; self.o += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.b, self.o)[0]; self.o += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.o)[0]; self.o += 4; return v

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.o)[0]; self.o += 4; return v

    def f32(self):
        v = struct.unpack_from("<f", self.b, self.o)[0]; self.o += 4; return v

    def f64(self):
        v = struct.unpack_from("<d", self.b, self.o)[0]; self.o += 8; return v

    def sstr(self):
        n = self.u8()
        s = self.b[self.o:self.o + n].decode("utf-8", "replace")
        self.o += n
        return s


def argb(v):
    return "#{:08X} (a={:02X} r={:02X} g={:02X} b={:02X})".format(
        v, v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255)


def parse_piv(r):
    out = {}
    out["version"] = r.u8()
    out["width"] = r.u32()
    out["height"] = r.u32()
    out["u16_a"] = r.u16()
    out["u8_a"] = r.u8()
    out["background_argb"] = r.u32()
    out["background_name"] = r.sstr() if r.b[r.o] and r.b[r.o] < 200 else None
    return out, r.o


def parse_stk(r):
    out = {"lead_u16": r.u16()}
    out["u32_a"] = r.u32()
    # first vertex floats follow in v5 (model: TVertex/TSegment)
    floats = []
    for _ in range(4):
        try:
            floats.append(round(r.f32(), 3))
        except Exception:
            break
    out["first_floats"] = floats
    return out, r.o


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--hex", type=int, default=0, help="dump N inflated bytes")
    args = ap.parse_args()

    payload, container = load(args.file)
    print(f"{args.file}: container={container} inflated={len(payload)} bytes")
    if args.hex:
        print(payload[:args.hex].hex(" ", 4))

    r = R(payload)
    if args.file.lower().endswith(".piv"):
        fields, off = parse_piv(r)
    else:
        fields, off = parse_stk(r)
    for k, v in fields.items():
        if k.endswith("argb"):
            print(f"  {k}: {argb(v)}")
        else:
            print(f"  {k}: {v}")
    print(f"  (parsed {off} bytes of {len(payload)})")


if __name__ == "__main__":
    main()
