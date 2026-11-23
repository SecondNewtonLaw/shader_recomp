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
; shader hash: 459e66b67cfb0bc45256f7d9ca47ea1e
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
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Input                             texture    byte         r/o      T0             t0     1
; Output                                UAV    byte         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%struct.ByteAddressBuffer = type { i32 }
%struct.RWByteAddressBuffer = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %4 = shl i32 %3, 5
  %5 = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %2, i32 %4, i32 undef)  ; BufferLoad(srv,index,wot)
  %6 = extractvalue %dx.types.ResRet.i32 %5, 0
  %7 = extractvalue %dx.types.ResRet.i32 %5, 1
  %8 = extractvalue %dx.types.ResRet.i32 %5, 2
  %9 = extractvalue %dx.types.ResRet.i32 %5, 3
  %10 = or i32 %4, 16
  %11 = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %2, i32 %10, i32 undef)  ; BufferLoad(srv,index,wot)
  %12 = extractvalue %dx.types.ResRet.i32 %11, 0
  %13 = extractvalue %dx.types.ResRet.i32 %11, 1
  %14 = xor i32 %12, %6
  %15 = xor i32 %13, %7
  %16 = xor i32 %12, %8
  %17 = xor i32 %13, %9
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %4, i32 undef, i32 %14, i32 %15, i32 %16, i32 %17, i8 15)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %18 = add i32 %13, %6
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %10, i32 undef, i32 %18, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %19 = lshr i32 %7, 7
  %20 = shl i32 %8, 25
  %21 = or i32 %19, %20
  %22 = or i32 %4, 20
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %22, i32 undef, i32 %21, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %23 = mul i32 %6, -1640531535
  %24 = xor i32 %12, %23
  %25 = or i32 %4, 24
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %25, i32 undef, i32 %24, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32, %dx.types.Handle, i32, i32) #1

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
!dx.entryPoints = !{!9}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{!5, !7, null, null}
!5 = !{!6}
!6 = !{i32 0, %struct.ByteAddressBuffer* undef, !"", i32 0, i32 0, i32 1, i32 11, i32 0, null}
!7 = !{!8}
!8 = !{i32 0, %struct.RWByteAddressBuffer* undef, !"", i32 0, i32 0, i32 1, i32 11, i1 false, i1 false, i1 false, null}
!9 = !{void ()* @main, !"main", null, !4, !10}
!10 = !{i32 0, i64 16, i32 4, !11}
!11 = !{i32 64, i32 1, i32 1}
