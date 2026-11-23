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
; shader hash: b8510bdfe528e2008799841c90b6850b
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
;   uint $Element;                                    ; Offset:    0 Size:     4
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
%"class.RWStructuredBuffer<unsigned int>" = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %3 = call i32 @dx.op.waveGetLaneIndex(i32 111)  ; WaveGetLaneIndex()
  %4 = call i32 @dx.op.waveGetLaneCount(i32 112)  ; WaveGetLaneCount()
  %5 = and i32 %2, 1
  %6 = icmp ne i32 %5, 0
  %7 = call %dx.types.fouri32 @dx.op.waveActiveBallot(i32 116, i1 %6)  ; WaveActiveBallot(cond)
  %8 = extractvalue %dx.types.fouri32 %7, 0
  %9 = extractvalue %dx.types.fouri32 %7, 1
  %10 = extractvalue %dx.types.fouri32 %7, 2
  %11 = extractvalue %dx.types.fouri32 %7, 3
  %12 = shl i32 %2, 3
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %12, i32 0, i32 %8, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %13 = or i32 %12, 1
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %13, i32 0, i32 %9, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %14 = or i32 %12, 2
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %14, i32 0, i32 %10, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %15 = or i32 %12, 3
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %15, i32 0, i32 %11, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %16 = call i1 @dx.op.waveAllTrue(i32 114, i1 %6)  ; WaveAllTrue(cond)
  %17 = zext i1 %16 to i32
  %18 = or i32 %12, 4
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %18, i32 0, i32 %17, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %19 = call i1 @dx.op.waveAnyTrue(i32 113, i1 %6)  ; WaveAnyTrue(cond)
  %20 = zext i1 %19 to i32
  %21 = or i32 %12, 5
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %21, i32 0, i32 %20, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %22 = call i32 @dx.op.waveAllOp(i32 135, i1 %6)  ; WaveAllBitCount(value)
  %23 = or i32 %12, 6
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %23, i32 0, i32 %22, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %24 = shl i32 %4, 8
  %25 = or i32 %24, %3
  %26 = or i32 %12, 7
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %26, i32 0, i32 %25, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readonly
declare i32 @dx.op.waveGetLaneIndex(i32) #1

; Function Attrs: nounwind readnone
declare i32 @dx.op.waveGetLaneCount(i32) #0

; Function Attrs: nounwind
declare %dx.types.fouri32 @dx.op.waveActiveBallot(i32, i1) #2

; Function Attrs: nounwind
declare i1 @dx.op.waveAnyTrue(i32, i1) #2

; Function Attrs: nounwind
declare i1 @dx.op.waveAllTrue(i32, i1) #2

; Function Attrs: nounwind
declare i32 @dx.op.waveAllOp(i32, i1) #2

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

; Function Attrs: nounwind
declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8) #2

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
!6 = !{i32 0, %"class.RWStructuredBuffer<unsigned int>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !7}
!7 = !{i32 1, i32 4}
!8 = !{void ()* @main, !"main", null, !4, !9}
!9 = !{i32 0, i64 524304, i32 4, !10}
!10 = !{i32 64, i32 1, i32 1}
