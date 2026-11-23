;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Position              0   xyzw        0      POS   float       
; TEXCOORD                 0   xy          1     NONE   float   xy  
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
;
; shader hash: 8e0be43b1978fdd82e43b2cc5a354108
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Pixel Shader
; DepthOutput=0
; SampleFrequency=0
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 2
; SigOutputElements: 1
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 2
; SigOutputVectors[0]: 1
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: main
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Position              0          noperspective       
; TEXCOORD                 0                 linear       
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Target                0                              
;
; Buffer Definitions:
;
; cbuffer Params
; {
;
;   struct Params
;   {
;
;       float2 uvScale;                               ; Offset:    0
;       float lod;                                    ; Offset:    8
;   
;   } Params;                                         ; Offset:    0 Size:    12
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Params                            cbuffer      NA          NA     CB0            cb0     1
; LinearSampler                     sampler      NA          NA      S0             s0     1
; Texture                           texture     f32          2d      T0             t0     1
;
;
; ViewId state:
;
; Number of inputs: 6, outputs: 4
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 4, 5 }
;   output 1 depends on inputs: { 4, 5 }
;   output 2 depends on inputs: { 4, 5 }
;   output 3 depends on inputs: { 4, 5 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%Params = type { <2 x float>, float }
%struct.SamplerState = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %4 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %5 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %6 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %3, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %7 = extractvalue %dx.types.CBufRet.f32 %6, 0
  %8 = extractvalue %dx.types.CBufRet.f32 %6, 1
  %9 = fmul fast float %7, %4
  %10 = fmul fast float %8, %5
  %11 = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %1, %dx.types.Handle %2, float %9, float %10, float undef, float undef, i32 0, i32 0, i32 undef, float undef)  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)
  %12 = extractvalue %dx.types.ResRet.f32 %11, 0
  %13 = extractvalue %dx.types.ResRet.f32 %11, 1
  %14 = extractvalue %dx.types.ResRet.f32 %11, 2
  %15 = extractvalue %dx.types.ResRet.f32 %11, 3
  %16 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %3, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %17 = extractvalue %dx.types.CBufRet.f32 %16, 2
  %18 = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %1, %dx.types.Handle %2, float %9, float %10, float undef, float undef, i32 0, i32 0, i32 undef, float %17)  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)
  %19 = extractvalue %dx.types.ResRet.f32 %18, 0
  %20 = extractvalue %dx.types.ResRet.f32 %18, 1
  %21 = extractvalue %dx.types.ResRet.f32 %18, 2
  %22 = extractvalue %dx.types.ResRet.f32 %18, 3
  %23 = call float @dx.op.unary.f32(i32 84, float %9)  ; DerivCoarseY(value)
  %24 = call float @dx.op.unary.f32(i32 84, float %10)  ; DerivCoarseY(value)
  %25 = call float @dx.op.unary.f32(i32 83, float %9)  ; DerivCoarseX(value)
  %26 = call float @dx.op.unary.f32(i32 83, float %10)  ; DerivCoarseX(value)
  %27 = call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, %dx.types.Handle %1, %dx.types.Handle %2, float %9, float %10, float undef, float undef, i32 0, i32 0, i32 undef, float %25, float %26, float undef, float %23, float %24, float undef, float undef)  ; SampleGrad(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,ddx0,ddx1,ddx2,ddy0,ddy1,ddy2,clamp)
  %28 = extractvalue %dx.types.ResRet.f32 %27, 0
  %29 = extractvalue %dx.types.ResRet.f32 %27, 1
  %30 = extractvalue %dx.types.ResRet.f32 %27, 2
  %31 = extractvalue %dx.types.ResRet.f32 %27, 3
  %32 = call %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32 61, %dx.types.Handle %1, %dx.types.Handle %2, float %9, float %10, float undef, float undef, i32 0, i32 0, i32 undef, float 5.000000e-01, float undef)  ; SampleBias(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,bias,clamp)
  %33 = extractvalue %dx.types.ResRet.f32 %32, 0
  %34 = extractvalue %dx.types.ResRet.f32 %32, 1
  %35 = extractvalue %dx.types.ResRet.f32 %32, 2
  %36 = extractvalue %dx.types.ResRet.f32 %32, 3
  %37 = fadd fast float %19, %12
  %38 = fadd fast float %20, %13
  %39 = fadd fast float %21, %14
  %40 = fadd fast float %22, %15
  %41 = fadd fast float %37, %28
  %42 = fadd fast float %38, %29
  %43 = fadd fast float %39, %30
  %44 = fadd fast float %40, %31
  %45 = fadd fast float %41, %33
  %46 = fadd fast float %42, %34
  %47 = fadd fast float %43, %35
  %48 = fadd fast float %44, %36
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %45)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %46)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %47)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %48)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sample.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sampleBias.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float, float, float, float, float, float, float) #2

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #2

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
attributes #2 = { nounwind readonly }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.viewIdState = !{!12}
!dx.entryPoints = !{!13}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"ps", i32 6, i32 0}
!4 = !{!5, null, !8, !10}
!5 = !{!6}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{!9}
!9 = !{i32 0, %Params* undef, !"", i32 0, i32 0, i32 1, i32 12, null}
!10 = !{!11}
!11 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!12 = !{[8 x i32] [i32 6, i32 4, i32 0, i32 0, i32 0, i32 0, i32 15, i32 15]}
!13 = !{void ()* @main, !"main", !14, !4, null}
!14 = !{!15, !20, null}
!15 = !{!16, !18}
!16 = !{i32 0, !"SV_Position", i8 9, i8 3, !17, i8 4, i32 1, i8 4, i32 0, i8 0, null}
!17 = !{i32 0}
!18 = !{i32 1, !"TEXCOORD", i8 9, i8 0, !17, i8 2, i32 1, i8 2, i32 1, i8 0, !19}
!19 = !{i32 3, i32 3}
!20 = !{!21}
!21 = !{i32 0, !"SV_Target", i8 9, i8 16, !17, i8 0, i32 1, i8 4, i32 0, i8 0, !22}
!22 = !{i32 3, i32 15}
