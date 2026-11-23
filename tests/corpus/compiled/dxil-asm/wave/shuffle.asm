;
; Note: shader requires additional functionality:
;       Wave level operations
;
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
; shader hash: 3101a092d45d12af47e6d3b59bfcf68a
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
; Resource bind info for Output
; {
;
;   float $Element;                                   ; Offset:    0 Size:     4
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Output                                UAV  struct         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.fouri32 = type { i32, i32, i32, i32 }
%"class.RWStructuredBuffer<float>" = type { float }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %3 = call i32 @dx.op.waveGetLaneIndex(i32 111)  ; WaveGetLaneIndex()
  %4 = call i32 @dx.op.waveGetLaneCount(i32 112)  ; WaveGetLaneCount()
  %5 = add i32 %2, 1
  %6 = uitofp i32 %5 to float
  %7 = add i32 %3, 1
  %8 = urem i32 %7, %4
  %9 = call float @dx.op.waveReadLaneAt.f32(i32 117, float %6, i32 %8)  ; WaveReadLaneAt(value,lane)
  %10 = add i32 %3, -1
  %11 = add i32 %10, %4
  %12 = urem i32 %11, %4
  %13 = call float @dx.op.waveReadLaneAt.f32(i32 117, float %6, i32 %12)  ; WaveReadLaneAt(value,lane)
  %14 = call float @dx.op.waveReadLaneFirst.f32(i32 118, float %6)  ; WaveReadLaneFirst(value)
  %15 = call %dx.types.fouri32 @dx.op.waveActiveBallot(i32 116, i1 true)  ; WaveActiveBallot(cond)
  %16 = extractvalue %dx.types.fouri32 %15, 0
  %17 = call i32 @dx.op.unaryBits.i32(i32 32, i32 %16)  ; FirstbitLo(value)
  %18 = call float @dx.op.waveReadLaneAt.f32(i32 117, float %6, i32 %17)  ; WaveReadLaneAt(value,lane)
  %19 = shl i32 %2, 2
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %19, i32 0, float %9, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %20 = or i32 %19, 1
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %20, i32 0, float %13, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %21 = or i32 %19, 2
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %21, i32 0, float %14, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %22 = or i32 %19, 3
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %22, i32 0, float %18, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readonly
declare i32 @dx.op.waveGetLaneIndex(i32) #1

; Function Attrs: nounwind readnone
declare i32 @dx.op.waveGetLaneCount(i32) #0

; Function Attrs: nounwind
declare float @dx.op.waveReadLaneAt.f32(i32, float, i32) #2

; Function Attrs: nounwind
declare float @dx.op.waveReadLaneFirst.f32(i32, float) #2

; Function Attrs: nounwind
declare %dx.types.fouri32 @dx.op.waveActiveBallot(i32, i1) #2

; Function Attrs: nounwind readnone
declare i32 @dx.op.unaryBits.i32(i32, i32) #0

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

; Function Attrs: nounwind
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8) #2

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind readonly }
attributes #2 = { nounwind }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!8}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{null, !5, null, null}
!5 = !{!6}
!6 = !{i32 0, %"class.RWStructuredBuffer<float>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !7}
!7 = !{i32 1, i32 4}
!8 = !{void ()* @main, !"main", null, !4, !9}
!9 = !{i32 0, i64 524304, i32 4, !10}
!10 = !{i32 64, i32 1, i32 1}
