;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; no parameters
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; no parameters
; shader hash: 6b03723587bf5ba75f3e3c145b79553f
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Compute Shader
; NumThreads=(64,1,1)
; NumBytesGroupSharedMemory: 0
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 0
; SigOutputElements: 0
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 0
; SigOutputVectors[0]: 0
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: main
;
;
; Buffer Definitions:
;
; cbuffer Params
; {
;
;   struct Params
;   {
;
;       float4 scale;                                 ; Offset:    0
;       uint mode;                                    ; Offset:   16
;   
;   } Params;                                         ; Offset:    0 Size:    20
;
; }
;
; Resource bind info for Input
; {
;
;   float4 $Element;                                  ; Offset:    0 Size:    16
;
; }
;
; Resource bind info for Output
; {
;
;   float4 $Element;                                  ; Offset:    0 Size:    16
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Params                            cbuffer      NA          NA     CB0            cb0     1
; Input                             texture  struct         r/o      T0             t0     1
; Output                                UAV  struct         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%"class.StructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%"class.RWStructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%Params = type { <4 x float>, i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %4 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %5 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %4, i32 0)  ; BufferLoad(srv,index,wot)
  %6 = extractvalue %dx.types.ResRet.f32 %5, 0
  %7 = extractvalue %dx.types.ResRet.f32 %5, 1
  %8 = extractvalue %dx.types.ResRet.f32 %5, 2
  %9 = add i32 %4, 1
  %10 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %9, i32 0)  ; BufferLoad(srv,index,wot)
  %11 = extractvalue %dx.types.ResRet.f32 %10, 0
  %12 = extractvalue %dx.types.ResRet.f32 %10, 1
  %13 = extractvalue %dx.types.ResRet.f32 %10, 2
  %14 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %3, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %15 = extractvalue %dx.types.CBufRet.f32 %14, 0
  %16 = extractvalue %dx.types.CBufRet.f32 %14, 2
  %17 = call float @dx.op.tertiary.f32(i32 46, float %6, float %11, float %15)  ; FMad(a,b,c)
  %18 = call float @dx.op.tertiary.f32(i32 46, float %8, float %13, float %16)  ; FMad(a,b,c)
  %19 = call float @dx.op.unary.f32(i32 7, float %17)  ; Saturate(value)
  %20 = call float @dx.op.unary.f32(i32 7, float %18)  ; Saturate(value)
  %21 = bitcast float %19 to i32
  %22 = shl i32 %21, 5
  %23 = lshr i32 %21, 27
  %24 = or i32 %22, %23
  %25 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %3, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %26 = extractvalue %dx.types.CBufRet.i32 %25, 0
  %27 = xor i32 %24, %26
  %28 = and i32 %27, 65535
  %29 = uitofp i32 %28 to float
  %30 = call float @dx.op.binary.f32(i32 35, float %20, float -2.000000e+00)  ; FMax(a,b)
  %31 = call float @dx.op.binary.f32(i32 36, float %30, float 2.000000e+00)  ; FMin(a,b)
  %32 = call float @dx.op.dot3.f32(i32 55, float %6, float %7, float %8, float %11, float %12, float %13)  ; Dot3(ax,ay,az,bx,by,bz)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %4, i32 0, float %19, float %29, float %31, float %32, i8 15)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %dx.types.Handle, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

; Function Attrs: nounwind
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32) #1

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind readonly }
attributes #2 = { nounwind }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!12}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{!5, !8, !10, null}
!5 = !{!6}
!6 = !{i32 0, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 16}
!8 = !{!9}
!9 = !{i32 0, %"class.RWStructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !7}
!10 = !{!11}
!11 = !{i32 0, %Params* undef, !"", i32 0, i32 0, i32 1, i32 20, null}
!12 = !{void ()* @main, !"main", null, !4, !13}
!13 = !{i32 0, i64 16, i32 4, !14}
!14 = !{i32 64, i32 1, i32 1}
