;
; Note: shader requires additional functionality:
;       Use native low precision
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
; shader hash: 162ed45470c3d743f7808aa318eb596c
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
; Output                                UAV    byte         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.LinAlgMatrixC8M16N16U2S0 = type { i8* }
%dx.types.ResourceProperties = type { i32, i32 }
%struct.RWByteAddressBuffer = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 1 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %3 = alloca <16 x half>, align 2
  %4 = alloca <16 x half>, align 2
  %5 = bitcast <16 x half>* %3 to i8*
  call void @llvm.lifetime.start(i64 32, i8* %5) #0
  store <16 x half> zeroinitializer, <16 x half>* %3, align 2, !tbaa !10
  %6 = bitcast <16 x half>* %4 to i8*
  call void @llvm.lifetime.start(i64 32, i8* %6) #0
  store <16 x half> zeroinitializer, <16 x half>* %4, align 2, !tbaa !10
  %7 = and i32 %2, 15
  %8 = getelementptr <16 x half>, <16 x half>* %3, i32 0, i32 %7
  store half 0xH3C00, half* %8, align 2, !tbaa !13
  %9 = add i32 %2, 1
  %10 = and i32 %9, 15
  %11 = getelementptr <16 x half>, <16 x half>* %4, i32 0, i32 %10
  store half 0xH4000, half* %11, align 2, !tbaa !13
  %12 = load <16 x half>, <16 x half>* %4, align 2, !tbaa !10
  %13 = load <16 x half>, <16 x half>* %3, align 2, !tbaa !10
  %14 = call %dx.types.LinAlgMatrixC8M16N16U2S0 @dx.op.linAlgMatrixOuterProduct.mC8M16N16U2S0.v16f16.v16f16(i32 -2147483619, <16 x half> %13, <16 x half> %12)  ; LinAlgMatrixOuterProduct(vectorA,vectorB)
  %15 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 4107, i32 0 })  ; AnnotateHandle(res,props)  resource: RWByteAddressBuffer
  call void @dx.op.linAlgMatrixAccumulateToDescriptor.mC8M16N16U2S0(i32 -2147483621, %dx.types.LinAlgMatrixC8M16N16U2S0 %14, %dx.types.Handle %15, i32 0, i32 0, i32 4, i32 0)  ; LinAlgMatrixAccumulateToDescriptor(matrix,handle,offset,stride,layout,align)
  call void @llvm.lifetime.end(i64 32, i8* %6) #0
  call void @llvm.lifetime.end(i64 32, i8* %5) #0
  ret void
}

; Function Attrs: nounwind
declare void @llvm.lifetime.start(i64, i8* nocapture) #0

; Function Attrs: nounwind
declare void @llvm.lifetime.end(i64, i8* nocapture) #0

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #1

; Function Attrs: nounwind
declare %dx.types.LinAlgMatrixC8M16N16U2S0 @dx.op.linAlgMatrixOuterProduct.mC8M16N16U2S0.v16f16.v16f16(i32, <16 x half>, <16 x half>) #0

; Function Attrs: nounwind
declare void @dx.op.linAlgMatrixAccumulateToDescriptor.mC8M16N16U2S0(i32, %dx.types.LinAlgMatrixC8M16N16U2S0, %dx.types.Handle, i32, i32, i32, i32) #0

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties) #1

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.createHandleFromBinding(i32, %dx.types.ResBind, i32, i1) #1

attributes #0 = { nounwind }
attributes #1 = { nounwind readnone }

!dx.targetTypes = !{!0}
!llvm.ident = !{!1}
!dx.version = !{!2}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!7}

!0 = !{%dx.types.LinAlgMatrixC8M16N16U2S0 undef, i32 8, i32 16, i32 16, i32 2, i32 0}
!1 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 10}
!4 = !{null, !5, null, null}
!5 = !{!6}
!6 = !{i32 0, %struct.RWByteAddressBuffer* undef, !"", i32 0, i32 0, i32 1, i32 11, i1 false, i1 false, i1 false, null}
!7 = !{void ()* @main, !"main", null, !4, !8}
!8 = !{i32 0, i64 8598323248, i32 4, !9}
!9 = !{i32 32, i32 1, i32 1}
!10 = !{!11, !11, i64 0}
!11 = !{!"omnipotent char", !12, i64 0}
!12 = !{!"Simple C/C++ TBAA"}
!13 = !{!14, !14, i64 0}
!14 = !{!"half", !11, i64 0}
