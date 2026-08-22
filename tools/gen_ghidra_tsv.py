#!/usr/bin/env python3
"""Emit a TSV of Ghidra labeling operations from classes_full.json.

Ops (tab-separated: op, hex-va, name):
  VMT  <classtype_va>  RTTI_<Unit>.<Class>       symbol label on the VMT
  MTD  <method_va>     <Unit>.<Class>.<method>   function rename/create
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(HERE, "..", "docs", "research", "mappings", "classes_full.json")
out = os.path.join(HERE, "..", "mappings", "ghidra_rtti_labels.tsv")


def clean(n):
    for ch in "<>`\\/:*?\"'|":
        n = n.replace(ch, "_")
    return n.replace(" ", "_").replace("\t", "_").replace("$", "_")


def main():
    d = json.load(open(src, encoding="utf-8"))
    lines = []
    nm = 0
    for c in d["classes"]:
        unit = clean(c.get("unit") or "Unknown")
        cname = clean(c["name"])
        va = int(c["classtype_va"])
        if va:
            lines.append(f"VMT\t{va:X}\tRTTI_{unit}.{cname}")
        for m in c.get("published_methods", []):
            mva = int(m["va"])
            if mva:
                lines.append(f"MTD\t{mva:X}\t{unit}.{cname}.{clean(m['name'])}")
                nm += 1
    with open(out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    print(f"[+] {len(lines)} ops ({nm} method renames) -> {out}")


if __name__ == "__main__":
    main()
