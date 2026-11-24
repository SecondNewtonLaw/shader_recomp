#!/usr/bin/env python3
"""Generate the GFX instruction coverage report.

Inputs:
  * AMD machine-readable ISA XML (RDNA4 for the family breakdown; totals for
    the other generations).
  * SR.Frontend/src/Arch/Gfx/GfxIsa.cpp (the lineage tables).
  * SR.Frontend/src/Arch/Gfx/GfxLifter.cpp (the lift rules).

Outputs:
  docs/coverage.json and docs/coverage.html.

Usage:
  python tools/coverage.py [path/to/amdgpu_isa_rdna4.xml]
"""

import collections
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")

# Instruction totals parsed from the AMD machine-readable ISA XML.
TOTALS = {
    "gfx10": 1149,   # RDNA1
    "gfx1013": 1153, # PS5, RDNA2 family
    "gfx1030": 1153, # RDNA2
    "gfx1100": 1148, # RDNA3
    "gfx1150": 1208, # RDNA3.5
    "gfx1200": 1264, # RDNA4
}

def lifted_ops():
    """Opcodes the lifter actually dispatches.

    Derived from GfxLifter.cpp instead of a hand-maintained list: an opcode
    counts as lifted when it is mentioned outside a `case Op::X:` body that
    only reports the instruction is not modeled. Encoding-dispatched families
    (DS, MIMG, MUBUF, EXP, ...) never appear as case labels, so they are
    recognised by their mentions inside the lift functions.

    A case can still call a helper that refuses deeper, so this is an upper
    bound; the decoded count beside it is exact.
    """
    source_path = os.path.join(ROOT, "SR.Frontend", "src", "Arch", "Gfx", "GfxLifter.cpp")
    source = open(source_path, encoding="utf-8").read()

    refusals = []
    for match in re.finditer(r"case Op::\w+:", source):
        following = re.search(r"case Op::\w+:|default:", source[match.end():])
        stop = match.end() + following.start() if following else len(source)
        body = source[match.end():stop]
        if "LiftError(" in body or "return std::unexpected(" in body:
            refusals.append((match.end(), stop))

    lifted = set()
    for match in re.finditer(r"Op::(\w+)", source):
        position = match.start()
        if any(begin <= position < stop for begin, stop in refusals):
            continue
        lifted.add(match.group(1))
    return lifted



FAMILIES = [
    ("SOP1", "SOP1"), ("SOP2", "SOP2"), ("SOPC", "SOPC"),
    ("SOPP", "SOPP"), ("SOPK", "SOPK"),
    ("VOP1", "VOP1"), ("VOP2", "VOP2"), ("VOP3", "VOP3"),
    ("VOP3P", "VOP3P"), ("VOPC", "VOPC"), ("VOPD", "VOPD"),
    ("SMEM", "SMEM"), ("VDS", "DS"), ("VGLOBAL", "GLOBAL"),
    ("VFLAT", "FLAT"), ("VBUFFER", "BUFFER"), ("VSAMPLE", "IMAGE"),
    ("VSCRATCH", "SCRATCH"),
]


def parse_tables():
    source = open(os.path.join(ROOT, "SR.Frontend/src/Arch/Gfx/GfxIsa.cpp"), encoding="utf-8").read()

    def parse(name):
        match = re.search(r"constexpr std::array " + name + r" = \{(.*?)\n\};", source, re.S)
        if not match:
            raise SystemExit(f"cannot find {name}")
        entries = []
        for item in re.finditer(
            r"(Make|Remove)\(Encoding::(\w+),\s*(\d+)(?:,\s*Op::(\w+))?", match.group(1)
        ):
            entries.append({
                "kind": item.group(1),
                "encoding": item.group(2),
                "opcode": int(item.group(3)),
                "op": item.group(4),
            })
        return entries

    base = parse("kGfx10")
    gfx11 = parse("kGfx11")
    gfx12 = parse("kGfx12")

    def parse_include(path, array_name):
        if not os.path.exists(path):
            return []
        text = open(path, encoding="utf-8").read()
        match = re.search(r"constexpr std::array " + array_name + r" = \{(.*?)\n\};", text, re.S)
        if not match:
            return []
        entries = []
        for item in re.finditer(
            r"(Make|Remove)\(Encoding::(\w+),\s*(\d+)(?:,\s*Op::(\w+))?", match.group(1)
        ):
            entries.append({
                "kind": item.group(1),
                "encoding": item.group(2),
                "opcode": int(item.group(3)),
                "op": item.group(4),
            })
        return entries

    include = os.path.join(ROOT, "SR.Frontend/src/Arch/Gfx/GfxVectorCompareTables.inc")
    base.extend(parse_include(include, "kGfx10Compare"))
    gfx11.extend(parse_include(include, "kGfx11Compare"))
    gfx12.extend(parse_include(include, "kGfx12Compare"))

    def apply(table, delta):
        result = {(entry["encoding"], entry["opcode"]): entry for entry in table}
        for entry in delta:
            key = (entry["encoding"], entry["opcode"])
            if entry["kind"] == "Remove":
                result.pop(key, None)
            else:
                result[key] = entry
        return result

    return {
        "gfx10": {(e["encoding"], e["opcode"]): e for e in base},
        "gfx1013": {(e["encoding"], e["opcode"]): e for e in base},
        "gfx1030": {(e["encoding"], e["opcode"]): e for e in base},
        "gfx1100": apply(base, gfx11),
        "gfx1150": apply(base, gfx11),
        "gfx1200": apply(apply(base, gfx11).values(), gfx12),
    }


def family_of_encoding(name):
    for prefix, family in FAMILIES:
        if name.startswith("ENC_" + prefix):
            return family
    return "OTHER"


DECODED_FAMILY = {
    "Sop1": "SOP1",
    "Sop2": "SOP2",
    "SopC": "SOPC",
    "SopP": "SOPP",
    "SopK": "SOPK",
    "Vop1": "VOP1",
    "Vop2": "VOP2",
    "Vop3": "VOP3",
    "Vop3P": "VOP3P",
    "VopC": "VOPC",
    "Smem": "SMEM",
    "Ds": "DS",
    "Global": "GLOBAL",
    "Flat": "FLAT",
    "Buffer": "BUFFER",
    "Scratch": "SCRATCH",
}


def gfx1200_families(xml_path):
    isa = ET.parse(xml_path).getroot().find("ISA")
    totals = collections.Counter()
    for instruction in isa.findall("./Instructions/Instruction"):
        encodings = [
            encoding.findtext("EncodingName") or ""
            for encoding in instruction.findall("./InstructionEncodings/InstructionEncoding")
        ]
        encodings = [name for name in encodings if name.startswith("ENC_")]
        if encodings:
            totals[family_of_encoding(encodings[0])] += 1
    return totals


def main():
    xml_path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("AMDGPU_ISA_XML")
    if not xml_path or not os.path.exists(xml_path):
        raise SystemExit("pass the path to amdgpu_isa_rdna4.xml or set AMDGPU_ISA_XML")

    lifted = lifted_ops()
    tables = parse_tables()
    report = {"generations": {}, "gfx1200_families": {}}

    for arch, table in tables.items():
        decoded = len(table)
        count = sum(1 for entry in table.values() if entry["op"] in lifted)
        report["generations"][arch] = {
            "total": TOTALS[arch],
            "decoded": decoded,
            "lifted": count,
            # A case can still refuse deeper; the decoded count is exact.
            "liftedIsUpperBound": True,
        }

    totals = gfx1200_families(xml_path)
    decoded_families = collections.Counter()
    lifted_families = collections.Counter()
    for entry in tables["gfx1200"].values():
        family = DECODED_FAMILY.get(entry["encoding"], "OTHER")
        decoded_families[family] += 1
        if entry["op"] in lifted:
            lifted_families[family] += 1

    for family, total in sorted(totals.items(), key=lambda item: -item[1]):
        report["gfx1200_families"][family] = {
            "total": total,
            "decoded": decoded_families[family],
            "lifted": lifted_families[family],
        }

    os.makedirs(DOCS, exist_ok=True)
    with open(os.path.join(DOCS, "coverage.json"), "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)

    generations = report["generations"]
    family_rows = report["gfx1200_families"]

    def percent(value, total):
        return 0.0 if total == 0 else value * 100.0 / total

    rows = []
    for name, data in generations.items():
        rows.append(
            f"""<div class="row">
  <div class="label">{name}</div>
  <div class="bar">
    <div class="lifted" style="width:{percent(data['lifted'], data['total']):.3f}%"></div>
    <div class="decoded" style="width:{percent(data['decoded'] - data['lifted'], data['total']):.3f}%"></div>
  </div>
  <div class="value">{data['lifted']} / {data['total']} lifted<br><span>{percent(data['decoded'], data['total']):.2f}% decoded</span></div>
</div>"""
        )

    family_html = []
    for name, data in family_rows.items():
        family_html.append(
            f"""<div class="row">
  <div class="label">{name}</div>
  <div class="bar">
    <div class="lifted" style="width:{percent(data['lifted'], data['total']):.3f}%"></div>
    <div class="decoded" style="width:{percent(data['decoded'] - data['lifted'], data['total']):.3f}%"></div>
  </div>
  <div class="value">{data['lifted']} / {data['total']}<br><span>{percent(data['decoded'], data['total']):.2f}%</span></div>
</div>"""
        )

    html = f"""<!doctype html>
<meta charset="utf-8">
<style>
  * {{ box-sizing: border-box; }}
  html, body {{ margin: 0; padding: 0; background: transparent; }}
  body {{ font-family: var(--font-sans, system-ui, sans-serif); color: var(--foreground, #e6e6e6); font-size: 13px; }}
  .wrap {{ display: flex; flex-direction: column; gap: 16px; padding: 4px 0 8px; }}
  h2 {{ font-size: 13px; font-weight: 600; margin: 0 0 6px; }}
  .panel {{ display: flex; flex-direction: column; gap: 6px; }}
  .row {{ display: grid; grid-template-columns: 64px 1fr 128px; gap: 10px; align-items: center; }}
  .label {{ font-family: var(--font-mono, ui-monospace, monospace); font-size: 11px; }}
  .bar {{ height: 14px; display: flex; overflow: hidden; border-radius: 4px; background: var(--secondary, #2a2a2a); }}
  .bar div {{ height: 100%; }}
  .lifted {{ background: var(--chart-2, #4ac26b); }}
  .decoded {{ background: var(--chart-1, #62a8e8); opacity: 0.45; }}
  .value {{ font-size: 11px; text-align: right; font-variant-numeric: tabular-nums; }}
  .value span {{ color: var(--muted-foreground, #9a9a9a); }}
  .legend {{ display: flex; gap: 14px; color: var(--muted-foreground, #9a9a9a); font-size: 11px; }}
  .legend i {{ display: inline-block; width: 12px; height: 8px; border-radius: 2px; margin-right: 5px; vertical-align: middle; }}
  .note {{ color: var(--muted-foreground, #9a9a9a); font-size: 11px; }}
</style>
<div class="wrap">
  <div class="panel">
    <h2>Instruction coverage by generation</h2>
    {''.join(rows)}
  </div>
  <div class="panel">
    <h2>gfx1200 by instruction family</h2>
    {''.join(family_html)}
  </div>
  <div class="legend">
    <span><i class="lifted" style="background:var(--chart-2,#4ac26b)"></i>lifted to IL</span>
    <span><i class="decoded" style="background:var(--chart-1,#62a8e8);opacity:.45"></i>decoded, not lifted</span>
    <span>denominator: AMD ISA XML instruction count</span>
  </div>
  <div class="note">gfx1200 has {generations['gfx1200']['decoded']} decoded opcodes and {generations['gfx1200']['lifted']} lifted to IL. The 29-vs-7 loop review is about emitted ISA quality, not coverage.</div>
</div>"""

    with open(os.path.join(DOCS, "coverage.html"), "w", encoding="utf-8") as handle:
        handle.write(html)

    print(json.dumps(report["generations"], indent=2))
    print("wrote docs/coverage.json and docs/coverage.html")


if __name__ == "__main__":
    main()
