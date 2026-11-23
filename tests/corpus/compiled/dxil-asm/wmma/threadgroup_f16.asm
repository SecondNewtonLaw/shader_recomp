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
; shader hash: 2adf435c03c9064fec1c76bc85c1d153
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Compute Shader
; NumThreads=(128,1,1)
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
; MatrixA                           texture    byte         r/o      T0             t0     1
; MatrixB                           texture    byte         r/o      T1             t1     1
; MatrixC                               UAV    byte         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.LinAlgMatrixC8M32N16U0S2 = type { i8* }
%dx.types.LinAlgMatrixC8M16N32U1S2 = type { i8* }
%dx.types.LinAlgMatrixC9M32N32U2S2 = type { i8* }
%struct.ByteAddressBuffer = type { i32 }
%struct.RWByteAddressBuffer = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 1 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 11, i32 0 })  ; AnnotateHandle(res,props)  resource: ByteAddressBuffer
  %5 = call %dx.types.LinAlgMatrixC8M32N16U0S2 @dx.op.linAlgMatrixLoadFromDescriptor.mC8M32N16U0S2(i32 -2147483634, %dx.types.Handle %4, i32 0, i32 32, i32 0, i32 128)  ; LinAlgMatrixLoadFromDescriptor(handle,offset,stride,layout,align)
  %6 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 11, i32 0 })  ; AnnotateHandle(res,props)  resource: ByteAddressBuffer
  %7 = call %dx.types.LinAlgMatrixC8M16N32U1S2 @dx.op.linAlgMatrixLoadFromDescriptor.mC8M16N32U1S2(i32 -2147483634, %dx.types.Handle %6, i32 0, i32 64, i32 0, i32 128)  ; LinAlgMatrixLoadFromDescriptor(handle,offset,stride,layout,align)
  %8 = call %dx.types.LinAlgMatrixC9M32N32U2S2 @dx.op.linAlgMatrixMultiply.mC9M32N32U2S2.mC8M32N16U0S2.mC8M16N32U1S2(i32 -2147483625, %dx.types.LinAlgMatrixC8M32N16U0S2 %5, %dx.types.LinAlgMatrixC8M16N32U1S2 %7)  ; LinAlgMatrixMultiply(matrixA,matrixB)
  %9 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 4107, i32 0 })  ; AnnotateHandle(res,props)  resource: RWByteAddressBuffer
  call void @dx.op.linAlgMatrixStoreToDescriptor.mC9M32N32U2S2(i32 -2147483628, %dx.types.LinAlgMatrixC9M32N32U2S2 %8, %dx.types.Handle %9, i32 0, i32 128, i32 0, i32 128)  ; LinAlgMatrixStoreToDescriptor(matrix,handle,offset,stride,layout,align)
  ret void
}

; Function Attrs: nounwind
declare %dx.types.LinAlgMatrixC8M32N16U0S2 @dx.op.linAlgMatrixLoadFromDescriptor.mC8M32N16U0S2(i32, %dx.types.Handle, i32, i32, i32, i32) #0

; Function Attrs: nounwind
declare %dx.types.LinAlgMatrixC8M16N32U1S2 @dx.op.linAlgMatrixLoadFromDescriptor.mC8M16N32U1S2(i32, %dx.types.Handle, i32, i32, i32, i32) #0

; Function Attrs: nounwind
declare %dx.types.LinAlgMatrixC9M32N32U2S2 @dx.op.linAlgMatrixMultiply.mC9M32N32U2S2.mC8M32N16U0S2.mC8M16N32U1S2(i32, %dx.types.LinAlgMatrixC8M32N16U0S2, %dx.types.LinAlgMatrixC8M16N32U1S2) #0

; Function Attrs: nounwind
declare void @dx.op.linAlgMatrixStoreToDescriptor.mC9M32N32U2S2(i32, %dx.types.LinAlgMatrixC9M32N32U2S2, %dx.types.Handle, i32, i32, i32, i32) #0

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties) #1

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.createHandleFromBinding(i32, %dx.types.ResBind, i32, i1) #1

attributes #0 = { nounwind }
attributes #1 = { nounwind readnone }

!dx.targetTypes = !{!0, !1, !2}
!llvm.ident = !{!3}
!dx.version = !{!4}
!dx.valver = !{!4}
!dx.shaderModel = !{!5}
!dx.resources = !{!6}
!dx.entryPoints = !{!12}

!0 = !{%dx.types.LinAlgMatrixC8M32N16U0S2 undef, i32 8, i32 32, i32 16, i32 0, i32 2}
!1 = !{%dx.types.LinAlgMatrixC8M16N32U1S2 undef, i32 8, i32 16, i32 32, i32 1, i32 2}
!2 = !{%dx.types.LinAlgMatrixC9M32N32U2S2 undef, i32 9, i32 32, i32 32, i32 2, i32 2}
!3 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!4 = !{i32 1, i32 10}
!5 = !{!"cs", i32 6, i32 10}
!6 = !{!7, !10, null, null}
!7 = !{!8, !9}
!8 = !{i32 0, %struct.ByteAddressBuffer* undef, !"", i32 0, i32 0, i32 1, i32 11, i32 0, null}
!9 = !{i32 1, %struct.ByteAddressBuffer* undef, !"", i32 0, i32 1, i32 1, i32 11, i32 0, null}
!10 = !{!11}
!11 = !{i32 0, %struct.RWByteAddressBuffer* undef, !"", i32 0, i32 0, i32 1, i32 11, i1 false, i1 false, i1 false, null}
!12 = !{void ()* @main, !"main", null, !6, !13}
!13 = !{i32 0, i64 8598323216, i32 4, !14}
!14 = !{i32 128, i32 1, i32 1}
