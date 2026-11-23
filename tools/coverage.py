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

LIFTED = {
    # Scalar ALU.
    "S_MovB32", "S_AddU32", "S_SubU32", "S_AddCoU32", "S_SubCoU32",
    "S_AddI32", "S_SubI32", "S_AddcU32", "S_SubbU32", "S_AddkI32",
    "S_MulkI32", "S_AndB32", "S_OrB32", "S_XorB32", "S_Andn2B32",
    "S_Orn2B32", "S_AndNot1B32", "S_OrNot1B32", "S_LshlB32",
    "S_LshrB32", "S_AshrI32", "S_CselectB32", "S_NotB32", "S_BrevB32",
    "S_Bcnt1I32B32", "S_Ff1I32B32", "S_FlbitI32B32", "S_AbsI32",
    "S_AbsDiffI32", "S_BfmB32", "S_BfeI32", "S_BfeU32", "S_MulI32",
    "S_MinI32", "S_MaxI32", "S_MinU32", "S_MaxU32", "S_NandB32",
    "S_NorB32", "S_XnorB32", "S_MovkI32",
    # Scalar compares and control flow.
    "S_CmpEqI32", "S_CmpLgI32", "S_CmpLtI32", "S_CmpLeI32",
    "S_CmpGtI32", "S_CmpGeI32", "S_CmpEqU32", "S_CmpLgU32",
    "S_CmpLtU32", "S_CmpLeU32", "S_CmpGtU32", "S_CmpGeU32",
    "S_Branch", "S_CbranchScc0", "S_CbranchScc1", "S_CbranchExecz",
    "S_CbranchExecnz", "S_CbranchVccz", "S_CbranchVccnz", "S_Endpgm",
    # 64-bit scalar.
    "S_MovB64", "S_NotB64", "S_AndB64", "S_OrB64", "S_XorB64",
    "S_CselectB64",
    # Memory.
    "S_LoadB32", "S_BufferLoadB32", "GlobalLoadB32", "GlobalStoreB32",
    # Vector ALU.
    "V_MovB32", "V_NotB32", "V_CvtF32I32", "V_CvtF32U32", "V_CvtI32F32",
    "V_CvtU32F32", "V_AddF32", "V_SubF32", "V_MulF32", "V_FmaF32",
    "V_MadF32", "V_MacF32", "V_AndB32", "V_OrB32", "V_XorB32",
    "V_MinI32", "V_MaxI32", "V_MinU32", "V_MaxU32", "V_LshlrevB32",
    "V_LshrrevB32", "V_AshrrevI32", "V_MulLoU32", "V_AddNcU32",
    "V_SubNcU32", "V_MinF32", "V_MaxF32", "V_Min3F32", "V_Max3F32",
    "V_RcpF32", "V_RsqF32", "V_SqrtF32", "V_ExpF32", "V_LogF32",
    "V_FfbhU32", "V_FfblB32", "V_ClzI32U32", "V_CtzI32B32",
    "V_BcntU32B32", "V_CndmaskB32",
    # VOP1 32-bit conversions and math.
    "V_Nop", "V_ReadfirstlaneB32", "V_CvtNearestI32F32", "V_CvtFloorI32F32",
    "V_CvtF32Ubyte0", "V_CvtF32Ubyte1", "V_CvtF32Ubyte2", "V_CvtF32Ubyte3",
    "V_FractF32", "V_TruncF32", "V_CeilF32", "V_RndneF32", "V_FloorF32",
    "V_SinF32", "V_CosF32", "V_BfrevB32", "V_ClsI32", "V_CvtI32I16",
    "V_CvtU32U16",
    # VOP2 32-bit multiply, reverse and carry family.
    "V_SubrevF32", "V_MulDx9ZeroF32", "V_MulI32I24", "V_MulHiI32I24",
    "V_MulU32U24", "V_MulHiU32U24", "V_XnorB32", "V_LshlrevB64",
    "V_AddCoCiU32", "V_SubCoCiU32", "V_SubrevCoCiU32", "V_SubrevNcU32",
    # Vector compares.
    "V_CmpLtI32", "V_CmpEqI32", "V_CmpLeI32", "V_CmpGtI32",
    "V_CmpNeI32", "V_CmpGeI32", "V_CmpLtU32", "V_CmpEqU32",
    "V_CmpLeU32", "V_CmpGtU32", "V_CmpNeU32", "V_CmpGeU32",
    "V_CmpLtF32", "V_CmpEqF32", "V_CmpLeF32", "V_CmpGtF32",
    "V_CmpLgF32", "V_CmpGeF32",
    # Compare predicates.
    "V_CmpFalseF32", "V_CmpOrdF32", "V_CmpUnordF32", "V_CmpNgeF32",
    "V_CmpNlgF32", "V_CmpNgtF32", "V_CmpNleF32", "V_CmpNeqF32",
    "V_CmpNltF32", "V_CmpTrueF32", "V_CmpFalseI32", "V_CmpTrueI32",
    "V_CmpFalseU32", "V_CmpTrueU32",
    # Bitfield.
    "V_BfeI32", "V_BfeU32", "V_BfiB32", "V_BfmB32", "V_AlignbitB32",
    "V_AlignbyteB32",
}

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

    tables = parse_tables()
    report = {"generations": {}, "gfx1200_families": {}}

    for arch, table in tables.items():
        decoded = len(table)
        lifted = sum(1 for entry in table.values() if entry["op"] in LIFTED)
        report["generations"][arch] = {
            "total": TOTALS[arch],
            "decoded": decoded,
            "lifted": lifted,
        }

    totals = gfx1200_families(xml_path)
    decoded_families = collections.Counter()
    lifted_families = collections.Counter()
    for entry in tables["gfx1200"].values():
        family = DECODED_FAMILY.get(entry["encoding"], "OTHER")
        decoded_families[family] += 1
        if entry["op"] in LIFTED:
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
