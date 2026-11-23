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
; shader hash: aa21302c8d4ae0edeac9be095d462714
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
%"class.RWStructuredBuffer<float>" = type { float }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %3 = add i32 %2, 1
  %4 = uitofp i32 %3 to float
  %5 = call float @dx.op.waveActiveOp.f32(i32 119, float %4, i8 0, i8 0)  ; WaveActiveOp(value,op,sop)
  %6 = call float @dx.op.waveActiveOp.f32(i32 119, float %4, i8 1, i8 0)  ; WaveActiveOp(value,op,sop)
  %7 = call float @dx.op.waveActiveOp.f32(i32 119, float %4, i8 2, i8 0)  ; WaveActiveOp(value,op,sop)
  %8 = call float @dx.op.waveActiveOp.f32(i32 119, float %4, i8 3, i8 0)  ; WaveActiveOp(value,op,sop)
  %9 = call float @dx.op.wavePrefixOp.f32(i32 121, float %4, i8 0, i8 0)  ; WavePrefixOp(value,op,sop)
  %10 = call i32 @dx.op.waveAllOp(i32 135, i1 true)  ; WaveAllBitCount(value)
  %11 = mul i32 %2, 6
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %11, i32 0, float %5, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %12 = or i32 %11, 1
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %12, i32 0, float %6, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %13 = add i32 %11, 2
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %13, i32 0, float %7, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %14 = add i32 %11, 3
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %14, i32 0, float %8, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %15 = add i32 %11, 4
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %15, i32 0, float %9, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %16 = uitofp i32 %10 to float
  %17 = add i32 %11, 5
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %17, i32 0, float %16, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind
declare float @dx.op.wavePrefixOp.f32(i32, float, i8, i8) #1

; Function Attrs: nounwind
declare float @dx.op.waveActiveOp.f32(i32, float, i8, i8) #1

; Function Attrs: nounwind
declare i32 @dx.op.waveAllOp(i32, i1) #1

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #2

; Function Attrs: nounwind
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8) #1

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
attributes #2 = { nounwind readonly }

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
