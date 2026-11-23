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
; shader hash: 9739361281e5d01bd00c403aa81f984f
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Compute Shader
; NumThreads=(32,1,1)
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
%dx.types.LinAlgMatrixC9M16N16U2S1 = type { i8* }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.LinAlgMatrixC21M16N32U0S1 = type { i8* }
%dx.types.LinAlgMatrixC21M32N16U1S1 = type { i8* }
%struct.ByteAddressBuffer = type { i32 }
%struct.RWByteAddressBuffer = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 1 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 0 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call %dx.types.LinAlgMatrixC9M16N16U2S1 @dx.op.linAlgFillMatrix.mC9M16N16U2S1.f32(i32 -2147483636, float 0.000000e+00)  ; LinAlgFillMatrix(value)
  %5 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 11, i32 0 })  ; AnnotateHandle(res,props)  resource: ByteAddressBuffer
  %6 = call %dx.types.LinAlgMatrixC21M16N32U0S1 @dx.op.linAlgMatrixLoadFromDescriptor.mC21M16N32U0S1(i32 -2147483634, %dx.types.Handle %5, i32 0, i32 32, i32 0, i32 128)  ; LinAlgMatrixLoadFromDescriptor(handle,offset,stride,layout,align)
  %7 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 11, i32 0 })  ; AnnotateHandle(res,props)  resource: ByteAddressBuffer
  %8 = call %dx.types.LinAlgMatrixC21M32N16U1S1 @dx.op.linAlgMatrixLoadFromDescriptor.mC21M32N16U1S1(i32 -2147483634, %dx.types.Handle %7, i32 0, i32 16, i32 0, i32 128)  ; LinAlgMatrixLoadFromDescriptor(handle,offset,stride,layout,align)
  %9 = call %dx.types.LinAlgMatrixC9M16N16U2S1 @dx.op.linAlgMatrixMultiplyAccumulate.mC9M16N16U2S1.mC21M16N32U0S1.mC21M32N16U1S1.mC9M16N16U2S1(i32 -2147483637, %dx.types.LinAlgMatrixC21M16N32U0S1 %6, %dx.types.LinAlgMatrixC21M32N16U1S1 %8, %dx.types.LinAlgMatrixC9M16N16U2S1 %4)  ; LinAlgMatrixMultiplyAccumulate(matrixA,matrixB,matrixC)
  %10 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 4107, i32 0 })  ; AnnotateHandle(res,props)  resource: RWByteAddressBuffer
  call void @dx.op.linAlgMatrixStoreToDescriptor.mC9M16N16U2S1(i32 -2147483628, %dx.types.LinAlgMatrixC9M16N16U2S1 %9, %dx.types.Handle %10, i32 0, i32 64, i32 0, i32 128)  ; LinAlgMatrixStoreToDescriptor(matrix,handle,offset,stride,layout,align)
  ret void
}

; Function Attrs: nounwind
declare %dx.types.LinAlgMatrixC9M16N16U2S1 @dx.op.linAlgFillMatrix.mC9M16N16U2S1.f32(i32, float) #0

; Function Attrs: nounwind
declare %dx.types.LinAlgMatrixC21M16N32U0S1 @dx.op.linAlgMatrixLoadFromDescriptor.mC21M16N32U0S1(i32, %dx.types.Handle, i32, i32, i32, i32) #0

; Function Attrs: nounwind
declare %dx.types.LinAlgMatrixC21M32N16U1S1 @dx.op.linAlgMatrixLoadFromDescriptor.mC21M32N16U1S1(i32, %dx.types.Handle, i32, i32, i32, i32) #0

; Function Attrs: nounwind
declare %dx.types.LinAlgMatrixC9M16N16U2S1 @dx.op.linAlgMatrixMultiplyAccumulate.mC9M16N16U2S1.mC21M16N32U0S1.mC21M32N16U1S1.mC9M16N16U2S1(i32, %dx.types.LinAlgMatrixC21M16N32U0S1, %dx.types.LinAlgMatrixC21M32N16U1S1, %dx.types.LinAlgMatrixC9M16N16U2S1) #0

; Function Attrs: nounwind
declare void @dx.op.linAlgMatrixStoreToDescriptor.mC9M16N16U2S1(i32, %dx.types.LinAlgMatrixC9M16N16U2S1, %dx.types.Handle, i32, i32, i32, i32) #0

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

!0 = !{%dx.types.LinAlgMatrixC9M16N16U2S1 undef, i32 9, i32 16, i32 16, i32 2, i32 1}
!1 = !{%dx.types.LinAlgMatrixC21M16N32U0S1 undef, i32 21, i32 16, i32 32, i32 0, i32 1}
!2 = !{%dx.types.LinAlgMatrixC21M32N16U1S1 undef, i32 21, i32 32, i32 16, i32 1, i32 1}
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
!14 = !{i32 32, i32 1, i32 1}
