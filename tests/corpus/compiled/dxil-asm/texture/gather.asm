;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; TEXCOORD                 0   xy          0     NONE   float   xy  
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Target                0   xyzw        0   TARGET   float   xyzw
;
; shader hash: 810f2fd859ce25d85fa4be3a0a67a862
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
; SigInputElements: 1
; SigOutputElements: 1
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 1
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
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; LinearSampler                     sampler      NA          NA      S0             s0     1
; ComparisonSampler                 sampler      NA          NA      S1             s1     1
; Texture                           texture     f32          2d      T0             t0     1
; ShadowMap                         texture     f32          2d      T1             t1     1
;
;
; ViewId state:
;
; Number of inputs: 2, outputs: 4
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 0, 1 }
;   output 1 depends on inputs: { 0, 1 }
;   output 2 depends on inputs: { 0, 1 }
;   output 3 depends on inputs: { 0, 1 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.Dimensions = type { i32, i32, i32, i32 }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%"class.Texture2D<float>" = type { float, %"class.Texture2D<float>::mips_type" }
%"class.Texture2D<float>::mips_type" = type { i32 }
%struct.SamplerState = type { i32 }
%struct.SamplerComparisonState = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 3, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %5 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %6 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %7 = call %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32 73, %dx.types.Handle %2, %dx.types.Handle %4, float %5, float %6, float undef, float undef, i32 0, i32 0, i32 0)  ; TextureGather(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,channel)
  %8 = extractvalue %dx.types.ResRet.f32 %7, 0
  %9 = extractvalue %dx.types.ResRet.f32 %7, 1
  %10 = extractvalue %dx.types.ResRet.f32 %7, 2
  %11 = extractvalue %dx.types.ResRet.f32 %7, 3
  %12 = call %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32 73, %dx.types.Handle %2, %dx.types.Handle %4, float %5, float %6, float undef, float undef, i32 1, i32 1, i32 0)  ; TextureGather(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,channel)
  %13 = extractvalue %dx.types.ResRet.f32 %12, 0
  %14 = extractvalue %dx.types.ResRet.f32 %12, 1
  %15 = extractvalue %dx.types.ResRet.f32 %12, 2
  %16 = extractvalue %dx.types.ResRet.f32 %12, 3
  %17 = call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65, %dx.types.Handle %1, %dx.types.Handle %3, float %5, float %6, float undef, float undef, i32 0, i32 0, i32 undef, float 5.000000e-01)  ; SampleCmpLevelZero(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,compareValue)
  %18 = extractvalue %dx.types.ResRet.f32 %17, 0
  %19 = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %1, i32 0)  ; GetDimensions(handle,mipLevel)
  %20 = extractvalue %dx.types.Dimensions %19, 0
  %21 = sitofp i32 %20 to float
  %22 = extractvalue %dx.types.Dimensions %19, 1
  %23 = sitofp i32 %22 to float
  %24 = fadd fast float %13, %8
  %25 = fadd fast float %14, %9
  %26 = fadd fast float %15, %10
  %27 = fadd fast float %16, %11
  %28 = fadd fast float %24, %18
  %29 = fadd fast float %25, %21
  %30 = fadd fast float %26, %23
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %28)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %29)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %30)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %27)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float) #2

; Function Attrs: nounwind readonly
declare %dx.types.Dimensions @dx.op.getDimensions(i32, %dx.types.Handle, i32) #2

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
!4 = !{!5, null, null, !9}
!5 = !{!6, !8}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{i32 1, %"class.Texture2D<float>"* undef, !"", i32 0, i32 1, i32 1, i32 2, i32 0, !7}
!9 = !{!10, !11}
!10 = !{i32 0, %struct.SamplerState* undef, !"", i32 0, i32 0, i32 1, i32 0, null}
!11 = !{i32 1, %struct.SamplerComparisonState* undef, !"", i32 0, i32 1, i32 1, i32 1, null}
!12 = !{[4 x i32] [i32 2, i32 4, i32 15, i32 15]}
!13 = !{void ()* @main, !"main", !14, !4, null}
!14 = !{!15, !19, null}
!15 = !{!16}
!16 = !{i32 0, !"TEXCOORD", i8 9, i8 0, !17, i8 2, i32 1, i8 2, i32 0, i8 0, !18}
!17 = !{i32 0}
!18 = !{i32 3, i32 3}
!19 = !{!20}
!20 = !{i32 0, !"SV_Target", i8 9, i8 16, !17, i8 0, i32 1, i8 4, i32 0, i8 0, !21}
!21 = !{i32 3, i32 15}
