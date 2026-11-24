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
    "S_CselectB64", "S_SextI32I8", "S_SextI32I16", "S_BrevB64",
    "S_Bcnt0I32B32", "S_Bcnt0I32B64", "S_Bcnt1I32B64", "S_Ff0I32B32",
    "S_Ff1I32B64", "S_FlbitI32B64", "S_FlbitI32", "S_FlbitI32I64",
    "S_Bitset0B32", "S_Bitset0B64", "S_Bitset1B32", "S_Bitset1B64",
    "S_LshlB64", "S_LshrB64", "S_AshrI64", "S_BfmB64",
    "S_BfeU64", "S_BfeI64", "S_MulHiI32", "S_MulHiU32",
    # frexp.
"V_FrexpExpI32F32", "V_FrexpMantF32", "V_FrexpExpI16F16", "V_FrexpMantF16",
    # VOP3 mask, byte pack, SAD16 and 64-bit shifts.
"V_CndmaskB16", "V_CvtPkU8F32", "V_SadU16", "V_LshrrevB64", "V_AshrrevI64",
    # 8-bit float conversions and dots.
"V_CvtF32Fp8", "V_CvtF32Bf8", "V_CvtPkF32Fp8", "V_CvtPkF32Bf8", "V_Dot4F32Fp8Fp8", "V_Dot4F32Bf8Bf8", "V_Dot4F32Fp8Bf8", "V_Dot4F32Bf8Fp8",
    # VOP3P dot products and mixed FMA.
"V_Dot2F32F16", "V_Dot2F32Bf16", "V_Dot4I32Iu8", "V_Dot4U32U8", "V_Dot8I32Iu4", "V_Dot8U32U4", "V_FmaMixF32", "V_FmaMixloF16", "V_FmaMixhiF16",
    # Extended VOP3 ALU and conversion forms.
"V_AddNcU16", "V_SubNcU16", "V_MulLoU16", "V_MinU16", "V_MaxU16", "V_AddNcI16", "V_SubNcI16", "V_MinI16", "V_MaxI16", "V_LshlrevB16", "V_LshrrevB16", "V_AshrrevI16", "V_MadU16", "V_MadI16", "V_MadU32U16", "V_MadI32I16", "V_Min3U16", "V_Min3I16", "V_Max3U16", "V_Max3I16", "V_Med3U16", "V_Med3I16", "V_Min3NumF16", "V_Max3NumF16", "V_Med3NumF32", "V_Med3NumF16", "V_Minimum3F32", "V_Maximum3F32", "V_Minimum3F16", "V_Maximum3F16", "V_MinmaxU32", "V_MaxminU32", "V_MinmaxI32", "V_MaxminI32", "V_MinmaxNumF32", "V_MaxminNumF32", "V_MinmaxNumF16", "V_MaxminNumF16", "V_MinimummaximumF32", "V_MaximumminimumF32", "V_MinimummaximumF16", "V_MaximumminimumF16", "V_MinimumF32", "V_MaximumF32", "V_MinimumF16", "V_MaximumF16", "V_MinNumF16", "V_MaxNumF16", "V_CvtPkI16F32", "V_CvtPkU16F32", "V_CvtPkI16I32", "V_CvtPkU16U32", "V_CvtPkNormI16F32", "V_CvtPkNormU16F32", "V_CvtPkNormI16F16", "V_CvtPkNormU16F16", "V_CvtPkRtzF16F32", "V_PackB32F16", "V_PkFmacF16", "V_SadU8", "V_SadHiU8", "V_MsadU8", "V_LerpU8", "V_CubeidF32", "V_CubemaF32", "V_CubescF32", "V_CubetcF32", "V_DivFixupF32", "V_MullitF32", "V_FmaDx9ZeroF32", "V_DivFmasF32", "V_SExpF32", "V_SExpF16", "V_SLogF32", "V_SLogF16", "V_SRcpF32", "V_SRcpF16", "V_SRsqF32", "V_SRsqF16", "V_SSqrtF32", "V_SSqrtF16", "V_RcpIflagF32",
    # Classification, exchange and frexp forms.
"V_CmpClassF16", "V_CmpClassF32", "V_CmpClassF64", "V_SwapB32", "V_SwapB16", "V_SatPkU8I16", "V_Pipeflush", "V_FrexpExpI32F32", "V_FrexpMantF32", "V_FrexpExpI16F16", "V_FrexpMantF16",
    # VOP3-encoded compares.
"V_CmpEqF16", "V_CmpEqF32", "V_CmpEqF64", "V_CmpEqI16", "V_CmpEqI32", "V_CmpEqI64", "V_CmpEqU16", "V_CmpEqU32", "V_CmpEqU64", "V_CmpGeF16", "V_CmpGeF32", "V_CmpGeF64", "V_CmpGeI16", "V_CmpGeI32", "V_CmpGeI64", "V_CmpGeU16", "V_CmpGeU32", "V_CmpGeU64", "V_CmpGtF16", "V_CmpGtF32", "V_CmpGtF64", "V_CmpGtI16", "V_CmpGtI32", "V_CmpGtI64", "V_CmpGtU16", "V_CmpGtU32", "V_CmpGtU64", "V_CmpLeF16", "V_CmpLeF32", "V_CmpLeF64", "V_CmpLeI16", "V_CmpLeI32", "V_CmpLeI64", "V_CmpLeU16", "V_CmpLeU32", "V_CmpLeU64", "V_CmpLgF16", "V_CmpLgF32", "V_CmpLgF64", "V_CmpLtF16", "V_CmpLtF32", "V_CmpLtF64", "V_CmpLtI16", "V_CmpLtI32", "V_CmpLtI64", "V_CmpLtU16", "V_CmpLtU32", "V_CmpLtU64", "V_CmpNeI16", "V_CmpNeI32", "V_CmpNeI64", "V_CmpNeU16", "V_CmpNeU32", "V_CmpNeU64", "V_CmpNeqF16", "V_CmpNeqF32", "V_CmpNeqF64", "V_CmpNgeF16", "V_CmpNgeF32", "V_CmpNgeF64", "V_CmpNgtF16", "V_CmpNgtF32", "V_CmpNgtF64", "V_CmpNleF16", "V_CmpNleF32", "V_CmpNleF64", "V_CmpNlgF16", "V_CmpNlgF32", "V_CmpNlgF64", "V_CmpNltF16", "V_CmpNltF32", "V_CmpNltF64", "V_CmpOrdF16", "V_CmpOrdF32", "V_CmpOrdF64", "V_CmpUnordF16", "V_CmpUnordF32", "V_CmpUnordF64",
    # Scalar control and immediate forms.
"S_Nop", "S_Clause", "S_DelayAlu", "S_WaitAlu", "S_Waitcnt", "S_WaitIdle", "S_WaitEvent", "S_RoundMode", "S_DenormMode", "S_BarrierWait", "S_Wakeup", "S_Setprio", "S_Incperflevel", "S_Decperflevel", "S_Ttracedata", "S_TtracedataImm", "S_IcacheInv", "S_WaitLoadcnt", "S_WaitStorecnt", "S_WaitSamplecnt", "S_WaitBvhcnt", "S_WaitExpcnt", "S_WaitDscnt", "S_WaitKmcnt", "S_WaitLoadcntDscnt", "S_WaitStorecntDscnt", "S_CodeEnd", "S_EndpgmSaved", "S_CmovkI32", "S_AddkCoI32",
    # Scalar compares, float arithmetic and conversions.
"S_Bitcmp0B32", "S_Bitcmp1B32", "S_Bitcmp0B64", "S_Bitcmp1B64", "S_CmpEqU64", "S_CmpLgU64", "S_Lshl1AddU32", "S_Lshl2AddU32", "S_Lshl3AddU32", "S_Lshl4AddU32", "S_PackLlB32B16", "S_PackLhB32B16", "S_PackHhB32B16", "S_PackHlB32B16", "S_NandB64", "S_NorB64", "S_XnorB64", "S_AndNot1B64", "S_OrNot1B64", "S_AddF32", "S_SubF32", "S_MulF32", "S_MinNumF32", "S_MaxNumF32", "S_MinimumF32", "S_MaximumF32", "S_FmacF32", "S_AddF16", "S_SubF16", "S_MulF16", "S_MinNumF16", "S_MaxNumF16", "S_MinimumF16", "S_MaximumF16", "S_FmacF16", "S_AddNcU64", "S_SubNcU64", "S_MulU64", "S_CmovB32", "S_CmovB64", "S_BitreplicateB64B32", "S_QuadmaskB32", "S_QuadmaskB64", "S_CeilF32", "S_FloorF32", "S_TruncF32", "S_RndneF32", "S_CeilF16", "S_FloorF16", "S_TruncF16", "S_RndneF16", "S_CvtF32I32", "S_CvtF32U32", "S_CvtI32F32", "S_CvtU32F32", "S_CvtF16F32", "S_CvtF32F16", "S_CvtHiF32F16", "S_CmpLtF32", "S_CmpEqF32", "S_CmpLeF32", "S_CmpGtF32", "S_CmpLgF32", "S_CmpGeF32", "S_CmpOF32", "S_CmpUF32", "S_CmpNgeF32", "S_CmpNlgF32", "S_CmpNgtF32", "S_CmpNleF32", "S_CmpNeqF32", "S_CmpNltF32", "S_CmpLtF16", "S_CmpEqF16", "S_CmpLeF16", "S_CmpGtF16", "S_CmpLgF16", "S_CmpGeF16", "S_CmpOF16", "S_CmpUF16", "S_CmpNgeF16", "S_CmpNlgF16", "S_CmpNgtF16", "S_CmpNleF16", "S_CmpNeqF16", "S_CmpNltF16",
    # Scalar memory widths and cache hints.
"S_LoadB64", "S_LoadB96", "S_LoadB128", "S_LoadB256", "S_LoadB512", "S_LoadI8", "S_LoadU8", "S_LoadI16", "S_LoadU16", "S_BufferLoadB64", "S_BufferLoadB96", "S_BufferLoadB128", "S_BufferLoadB256", "S_BufferLoadB512", "S_BufferLoadI8", "S_BufferLoadU8", "S_BufferLoadI16", "S_BufferLoadU16", "S_DcacheInv", "S_Gl1Inv", "S_AtcProbe", "S_AtcProbeBuffer", "S_PrefetchInst", "S_PrefetchInstPcRel", "S_PrefetchData", "S_BufferPrefetchData", "S_PrefetchDataPcRel",
    # Buffer (MUBUF).
"BufferLoadU8", "BufferLoadI8", "BufferLoadU16", "BufferLoadI16", "BufferLoadB32", "BufferLoadB64", "BufferLoadB96", "BufferLoadB128", "BufferStoreB8", "BufferStoreB16", "BufferStoreB32", "BufferStoreB64", "BufferStoreB96", "BufferStoreB128", "BufferLoadD16U8", "BufferLoadD16I8", "BufferLoadD16B16", "BufferLoadD16HiU8", "BufferLoadD16HiI8", "BufferLoadD16HiB16", "BufferStoreD16HiB8", "BufferStoreD16HiB16", "BufferAtomicSwapB32", "BufferAtomicCmpswapB32", "BufferAtomicAddU32", "BufferAtomicSubU32", "BufferAtomicSubClampU32", "BufferAtomicMinI32", "BufferAtomicMinU32", "BufferAtomicMaxI32", "BufferAtomicMaxU32", "BufferAtomicAndB32", "BufferAtomicOrB32", "BufferAtomicXorB32", "BufferAtomicIncU32", "BufferAtomicDecU32", "BufferAtomicSwapB64", "BufferAtomicCmpswapB64", "BufferAtomicAddU64", "BufferAtomicSubU64", "BufferAtomicMinI64", "BufferAtomicMinU64", "BufferAtomicMaxI64", "BufferAtomicMaxU64", "BufferAtomicAndB64", "BufferAtomicOrB64", "BufferAtomicXorB64", "BufferAtomicIncU64", "BufferAtomicDecU64", "BufferAtomicCondSubU32", "BufferAtomicMinNumF32", "BufferAtomicMaxNumF32", "BufferAtomicAddF32",
    # Flat, global and scratch memory.
"FlatLoadU8", "FlatLoadI8", "FlatLoadU16", "FlatLoadI16", "FlatLoadB32", "FlatLoadB64", "FlatLoadB96", "FlatLoadB128", "FlatStoreB8", "FlatStoreB16", "FlatStoreB32", "FlatStoreB64", "FlatStoreB96", "FlatStoreB128", "FlatLoadD16U8", "FlatLoadD16I8", "FlatLoadD16B16", "FlatLoadD16HiU8", "FlatLoadD16HiI8", "FlatLoadD16HiB16", "FlatStoreD16HiB8", "FlatStoreD16HiB16", "FlatAtomicSwapB32", "FlatAtomicCmpswapB32", "FlatAtomicAddU32", "FlatAtomicSubU32", "FlatAtomicSubClampU32", "FlatAtomicMinI32", "FlatAtomicMinU32", "FlatAtomicMaxI32", "FlatAtomicMaxU32", "FlatAtomicAndB32", "FlatAtomicOrB32", "FlatAtomicXorB32", "FlatAtomicIncU32", "FlatAtomicDecU32", "FlatAtomicSwapB64", "FlatAtomicCmpswapB64", "FlatAtomicAddU64", "FlatAtomicSubU64", "FlatAtomicMinI64", "FlatAtomicMinU64", "FlatAtomicMaxI64", "FlatAtomicMaxU64", "FlatAtomicAndB64", "FlatAtomicOrB64", "FlatAtomicXorB64", "FlatAtomicIncU64", "FlatAtomicDecU64", "FlatAtomicCondSubU32", "FlatAtomicMinNumF32", "FlatAtomicMaxNumF32", "FlatAtomicAddF32", "GlobalLoadU8", "GlobalLoadI8", "GlobalLoadU16", "GlobalLoadI16", "GlobalLoadB32", "GlobalLoadB64", "GlobalLoadB96", "GlobalLoadB128", "GlobalStoreB8", "GlobalStoreB16", "GlobalStoreB32", "GlobalStoreB64", "GlobalStoreB96", "GlobalStoreB128", "GlobalLoadD16U8", "GlobalLoadD16I8", "GlobalLoadD16B16", "GlobalLoadD16HiU8", "GlobalLoadD16HiI8", "GlobalLoadD16HiB16", "GlobalStoreD16HiB8", "GlobalStoreD16HiB16", "GlobalAtomicSwapB32", "GlobalAtomicCmpswapB32", "GlobalAtomicAddU32", "GlobalAtomicSubU32", "GlobalAtomicSubClampU32", "GlobalAtomicMinI32", "GlobalAtomicMinU32", "GlobalAtomicMaxI32", "GlobalAtomicMaxU32", "GlobalAtomicAndB32", "GlobalAtomicOrB32", "GlobalAtomicXorB32", "GlobalAtomicIncU32", "GlobalAtomicDecU32", "GlobalAtomicSwapB64", "GlobalAtomicCmpswapB64", "GlobalAtomicAddU64", "GlobalAtomicSubU64", "GlobalAtomicMinI64", "GlobalAtomicMinU64", "GlobalAtomicMaxI64", "GlobalAtomicMaxU64", "GlobalAtomicAndB64", "GlobalAtomicOrB64", "GlobalAtomicXorB64", "GlobalAtomicIncU64", "GlobalAtomicDecU64", "GlobalAtomicCondSubU32", "GlobalAtomicMinNumF32", "GlobalAtomicMaxNumF32", "GlobalAtomicAddF32", "GlobalAtomicOrderedAddB64", "ScratchLoadU8", "ScratchLoadI8", "ScratchLoadU16", "ScratchLoadI16", "ScratchLoadB32", "ScratchLoadB64", "ScratchLoadB96", "ScratchLoadB128", "ScratchStoreB8", "ScratchStoreB16", "ScratchStoreB32", "ScratchStoreB64", "ScratchStoreB96", "ScratchStoreB128", "ScratchLoadD16U8", "ScratchLoadD16I8", "ScratchLoadD16B16", "ScratchLoadD16HiU8", "ScratchLoadD16HiI8", "ScratchLoadD16HiB16", "ScratchStoreD16HiB8", "ScratchStoreD16HiB16",
    # Data share (LDS).
"DsAddF32", "DsAddRtnF32", "DsAddRtnU32", "DsAddRtnU64", "DsAddU32", "DsAddU64", "DsAndB32", "DsAndB64", "DsAndRtnB32", "DsAndRtnB64", "DsCmpstoreB32", "DsCmpstoreB64", "DsCmpstoreRtnB32", "DsCmpstoreRtnB64", "DsCondSubRtnU32", "DsCondSubU32", "DsDecRtnU32", "DsDecRtnU64", "DsDecU32", "DsDecU64", "DsIncRtnU32", "DsIncRtnU64", "DsIncU32", "DsIncU64", "DsLoad2addrB32", "DsLoad2addrB64", "DsLoad2addrStride64B32", "DsLoad2addrStride64B64", "DsLoadB128", "DsLoadB32", "DsLoadB64", "DsLoadB96", "DsLoadI16", "DsLoadI8", "DsLoadI8D16", "DsLoadI8D16Hi", "DsLoadU16", "DsLoadU16D16", "DsLoadU16D16Hi", "DsLoadU8", "DsLoadU8D16", "DsLoadU8D16Hi", "DsMaxI32", "DsMaxI64", "DsMaxNumF32", "DsMaxNumF64", "DsMaxNumRtnF32", "DsMaxNumRtnF64", "DsMaxRtnI32", "DsMaxRtnI64", "DsMaxRtnU32", "DsMaxRtnU64", "DsMaxU32", "DsMaxU64", "DsMinI32", "DsMinI64", "DsMinNumF32", "DsMinNumF64", "DsMinNumRtnF32", "DsMinNumRtnF64", "DsMinRtnI32", "DsMinRtnI64", "DsMinRtnU32", "DsMinRtnU64", "DsMinU32", "DsMinU64", "DsMskorB32", "DsMskorB64", "DsMskorRtnB32", "DsMskorRtnB64", "DsNop", "DsOrB32", "DsOrB64", "DsOrRtnB32", "DsOrRtnB64", "DsPkAddBf16", "DsPkAddF16", "DsPkAddRtnBf16", "DsPkAddRtnF16", "DsRsubRtnU32", "DsRsubRtnU64", "DsRsubU32", "DsRsubU64", "DsStore2addrB32", "DsStore2addrB64", "DsStore2addrStride64B32", "DsStore2addrStride64B64", "DsStoreB128", "DsStoreB16", "DsStoreB16D16Hi", "DsStoreB32", "DsStoreB64", "DsStoreB8", "DsStoreB8D16Hi", "DsStoreB96", "DsStorexchg2addrRtnB32", "DsStorexchg2addrRtnB64", "DsStorexchg2addrStride64RtnB32", "DsStorexchg2addrStride64RtnB64", "DsStorexchgRtnB32", "DsStorexchgRtnB64", "DsSubClampRtnU32", "DsSubClampU32", "DsSubRtnU32", "DsSubRtnU64", "DsSubU32", "DsSubU64", "DsXorB32", "DsXorB64", "DsXorRtnB32", "DsXorRtnB64",
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
    # VOP3 3-input integer operations.
    "V_Add3U32", "V_AddLshlU32", "V_AndOrB32", "V_Or3B32", "V_Xor3B32",
    "V_LshlAddU32", "V_LshlOrB32", "V_XadU32", "V_MulHiI32", "V_MulHiU32",
    "V_Min3I32", "V_Min3U32", "V_Med3I32", "V_Med3U32", "V_Max3I32",
    "V_Max3U32", "V_MadI32I24", "V_MadU32U24", "V_SadU32", "V_LdexpF32",
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
    # F16.
    "V_CvtF16F32", "V_CvtF32F16", "V_CvtF16U16", "V_CvtF16I16", "V_CvtU16F16",
    "V_CvtI16F16", "V_CvtNormI16F16", "V_CvtNormU16F16", "V_RcpF16", "V_RsqF16",
    "V_SqrtF16", "V_ExpF16", "V_LogF16", "V_FloorF16", "V_CeilF16", "V_TruncF16",
    "V_RndneF16", "V_FractF16", "V_SinF16", "V_CosF16", "V_MovB16", "V_NotB16",
    "V_AddF16", "V_SubF16", "V_SubrevF16", "V_MulF16", "V_FmacF16", "V_MinF16",
    "V_MaxF16", "V_LdexpF16", "V_Min3F16", "V_Max3F16", "V_FmaF16",
    # F64.
    "V_CvtI32F64", "V_CvtF64I32", "V_CvtF32F64", "V_CvtF64F32", "V_CvtU32F64",
    "V_CvtF64U32", "V_TruncF64", "V_CeilF64", "V_RndneF64", "V_FloorF64",
    "V_RcpF64", "V_RsqF64", "V_SqrtF64", "V_FractF64", "V_AddF64", "V_MulF64",
    "V_MinF64", "V_MaxF64", "V_FmaF64", "V_LdexpF64",
    # VOP3P packed.
    "V_PkMadI16", "V_PkMulLoU16", "V_PkAddI16", "V_PkSubI16", "V_PkLshlrevB16", "V_PkLshrrevB16", "V_PkAshrrevI16", "V_PkMaxI16", "V_PkMinI16", "V_PkMadU16", "V_PkAddU16", "V_PkSubU16", "V_PkMaxU16", "V_PkMinU16", "V_PkFmaF16", "V_PkAddF16", "V_PkMulF16", "V_PkMinF16", "V_PkMaxF16",
    # Wide compares.
    "V_CmpEqF16", "V_CmpEqF64", "V_CmpEqI16", "V_CmpEqI64", "V_CmpEqU16", "V_CmpEqU64", "V_CmpFalseF16", "V_CmpFalseF64", "V_CmpFalseI64", "V_CmpFalseU64", "V_CmpGeF16", "V_CmpGeF64", "V_CmpGeI16", "V_CmpGeI64", "V_CmpGeU16", "V_CmpGeU64", "V_CmpGtF16", "V_CmpGtF64", "V_CmpGtI16", "V_CmpGtI64", "V_CmpGtU16", "V_CmpGtU64", "V_CmpLeF16", "V_CmpLeF64", "V_CmpLeI16", "V_CmpLeI64", "V_CmpLeU16", "V_CmpLeU64", "V_CmpLgF16", "V_CmpLgF64", "V_CmpLtF16", "V_CmpLtF64", "V_CmpLtI16", "V_CmpLtI64", "V_CmpLtU16", "V_CmpLtU64", "V_CmpNeI16", "V_CmpNeI64", "V_CmpNeU16", "V_CmpNeU64", "V_CmpNeqF16", "V_CmpNeqF64", "V_CmpNgeF16", "V_CmpNgeF64", "V_CmpNgtF16", "V_CmpNgtF64", "V_CmpNleF16", "V_CmpNleF64", "V_CmpNlgF16", "V_CmpNlgF64", "V_CmpNltF16", "V_CmpNltF64", "V_CmpOrdF16", "V_CmpOrdF64", "V_CmpTrueF16", "V_CmpTrueF64", "V_CmpTrueI64", "V_CmpTrueU64", "V_CmpUnordF16", "V_CmpUnordF64",
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
