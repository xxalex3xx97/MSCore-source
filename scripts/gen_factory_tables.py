#!/usr/bin/env python3
# gen_factory_tables.py: writes the GENERATED block of mods/MSCore/Scripts/mscore_factory.lua.
# Table = probe kPasteMach (machine DAT -> w,h) and kPasteBlk (block class -> floor w,h, nofloor) from mb_bpprobe.hpp.
# catalog4.json is only a cross-check: mismatches and gaps go to stderr. The probe wins.
# Usage: python -I gen_factory_tables.py [--splice PATH] [--catalog PATH] [--probe PATH]
#   no --splice: print the Lua block to stdout.
import argparse
import json
import re
import sys
from pathlib import Path

BEGIN = "-- BEGIN GENERATED"
END = "-- END GENERATED"
CATALOG = Path(r"F:\MSDemoMods-public\tools\factory-blueprint\extract\catalog4.json")
PROBE = Path(r"F:\ms-factory-bp\mods\MiningBenchMod\mb_bpprobe.hpp")


def probe_tables(src):
    m = re.search(r"kPasteMach\[\][^=]*=(.*?)\};", src, re.S)
    b = re.search(r"kPasteBlk\[\][^=]*=(.*?)\};", src, re.S)
    if not m or not b:
        sys.exit("kPasteMach / kPasteBlk not found in probe source")
    mach = {d: (int(w), int(h)) for d, w, h in
            re.findall(r'\{"(DAT_\w+)"\s*,\s*(\d+)\s*,\s*(\d+)\}', m.group(1))}
    blk = {c: (int(w), int(h), nf == "true") for c, w, h, nf in
           re.findall(r'\{"(BPO_\w+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(true|false)\}', b.group(1))}
    return mach, blk


def cross_check(cat, mach, blk):
    cat_mach = {x["id"]: (x["w"], x["h"]) for x in cat["buildables"] if x.get("category") == "Machine"}
    for d, wh in sorted(mach.items()):
        if d not in cat_mach:
            print(f"catalog4: no Machine entry for {d} (probe {wh})", file=sys.stderr)
        elif cat_mach[d] != wh:
            print(f"MISMATCH {d}: probe {wh} catalog {cat_mach[d]}", file=sys.stderr)
    for d in sorted(set(cat_mach) - set(mach)):
        print(f"catalog4 machine not in probe table (not emitted): {d} {cat_mach[d]}", file=sys.stderr)
    cat_blk = {x["name"]: x["floor"] for x in cat.get("blocks", []) if x.get("floor")}
    for c, (w, h, nf) in sorted(blk.items()):
        if nf or c not in cat_blk:
            continue
        if (cat_blk[c]["w"], cat_blk[c]["h"]) != (w, h):
            print(f"MISMATCH floor {c}: probe {(w, h)} catalog {(cat_blk[c]['w'], cat_blk[c]['h'])}", file=sys.stderr)


def lua_block(mach, blk):
    out = [BEGIN + " (scripts/gen_factory_tables.py from mb_bpprobe.hpp kPasteMach/kPasteBlk; do not edit by hand)"]
    out.append("M.tables = { -- machine DAT -> footprint {w,h} in cells (half extents); odd rot swaps w and h")
    for d in sorted(mach):
        w, h = mach[d]
        out.append(f"    {d} = {{ w = {w}, h = {h} }},")
    out.append("}")
    out.append("M.block_floor = { -- block class -> floor {w,h} half extents in units; nofloor = no floor rect")
    for c in sorted(blk):
        w, h, nf = blk[c]
        if nf:
            out.append(f"    {c} = {{ w = 0, h = 0, nofloor = true }},")
        else:
            out.append(f"    {c} = {{ w = {w}, h = {h} }},")
    out.append("}")
    out.append(END)
    return "\n".join(out)


def splice(path, block):
    p = Path(path)
    lines = p.read_text(encoding="utf-8").split("\n")
    b = next(i for i, l in enumerate(lines) if l.startswith(BEGIN))
    e = next(i for i, l in enumerate(lines) if l.startswith(END))
    p.write_text("\n".join(lines[:b] + block.split("\n") + lines[e + 1:]), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--splice", help="mscore_factory.lua to rewrite between the markers")
    ap.add_argument("--catalog", default=str(CATALOG))
    ap.add_argument("--probe", default=str(PROBE))
    a = ap.parse_args()
    cat = json.loads(Path(a.catalog).read_text(encoding="utf-8"))
    mach, blk = probe_tables(Path(a.probe).read_text(encoding="utf-8", errors="replace"))
    # live-data fix (2026-11-08): Dock_2 fills a 64x64 pitch slot (Main_Base (2).json shipyard row), probe value 14x30 was a guess
    blk["BPO_Dock_2"] = (30, 30, False)
    cross_check(cat, mach, blk)
    block = lua_block(mach, blk)
    if a.splice:
        splice(a.splice, block)
        print(f"spliced {len(mach)} machines, {len(blk)} blocks into {a.splice}", file=sys.stderr)
    else:
        print(block)


if __name__ == "__main__":
    main()
