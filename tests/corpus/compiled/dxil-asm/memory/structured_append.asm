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
; shader hash: 93f9d4ab9544039583a6b5c4770aef24
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
; Resource bind info for Spawned
; {
;
;   struct struct.Particle
;   {
;
;       float3 position;                              ; Offset:    0
;       float life;                                   ; Offset:   12
;   
;   } $Element;                                       ; Offset:    0 Size:    16
;
; }
;
; Resource bind info for Dead
; {
;
;   struct struct.Particle
;   {
;
;       float3 position;                              ; Offset:    0
;       float life;                                   ; Offset:   12
;   
;   } $Element;                                       ; Offset:    0 Size:    16
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
; Spawned                               UAV  struct     r/w+cnt      U0             u0     1
; Dead                                  UAV  struct     r/w+cnt      U1             u1     1
; Output                                UAV  struct         r/w      U2             u2     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.AppendStructuredBuffer<Particle>" = type { %struct.Particle }
%struct.Particle = type { <3 x float>, float }
%"class.ConsumeStructuredBuffer<Particle>" = type { %struct.Particle }
%"class.RWStructuredBuffer<vector<float, 4> >" = type { <4 x float> }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 2, i32 2, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %4 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %5 = icmp ugt i32 %4, 15
  br i1 %5, label %19, label %6

; <label>:6                                       ; preds = %0
  %7 = call i32 @dx.op.bufferUpdateCounter(i32 70, %dx.types.Handle %2, i8 -1)  ; BufferUpdateCounter(uav,inc)
  %8 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %7, i32 0)  ; BufferLoad(srv,index,wot)
  %9 = extractvalue %dx.types.ResRet.f32 %8, 0
  %10 = extractvalue %dx.types.ResRet.f32 %8, 1
  %11 = extractvalue %dx.types.ResRet.f32 %8, 2
  %12 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %7, i32 12)  ; BufferLoad(srv,index,wot)
  %13 = extractvalue %dx.types.ResRet.f32 %12, 0
  %14 = fadd fast float %13, -1.250000e-01
  %15 = fcmp fast ogt float %14, 0.000000e+00
  br i1 %15, label %16, label %18

; <label>:16                                      ; preds = %6
  %17 = call i32 @dx.op.bufferUpdateCounter(i32 70, %dx.types.Handle %3, i8 1)  ; BufferUpdateCounter(uav,inc)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %3, i32 %17, i32 0, float %9, float %10, float %11, float undef, i8 7)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %3, i32 %17, i32 12, float %14, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  br label %18

; <label>:18                                      ; preds = %16, %6
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %4, i32 0, float %9, float %10, float %11, float %14, i8 15)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  br label %19

; <label>:19                                      ; preds = %18, %0
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind
declare i32 @dx.op.bufferUpdateCounter(i32, %dx.types.Handle, i8) #1

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #2

; Function Attrs: nounwind
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32) #2

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
attributes #2 = { nounwind readonly }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!10}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{null, !5, null, null}
!5 = !{!6, !8, !9}
!6 = !{i32 0, %"class.AppendStructuredBuffer<Particle>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 true, i1 false, !7}
!7 = !{i32 1, i32 16}
!8 = !{i32 1, %"class.ConsumeStructuredBuffer<Particle>"* undef, !"", i32 0, i32 1, i32 1, i32 12, i1 false, i1 true, i1 false, !7}
!9 = !{i32 2, %"class.RWStructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 2, i32 1, i32 12, i1 false, i1 false, i1 false, !7}
!10 = !{void ()* @main, !"main", null, !4, !11}
!11 = !{i32 0, i64 16, i32 4, !12}
!12 = !{i32 64, i32 1, i32 1}
