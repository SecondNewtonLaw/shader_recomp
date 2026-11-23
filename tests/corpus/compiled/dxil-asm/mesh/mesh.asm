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
; SV_Position              0   xyzw        0      POS   float   xyzw
; NORMAL                   0   xyz         1     NONE   float   xyz 
;
; shader hash: e71761245cb53e8b31ed07c9ec0dce73
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Mesh Shader
; MeshOutputTopology=triangle
; NumThreads=(32,1,1)
; NumBytesGroupSharedMemory: 0
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 0
; SigOutputElements: 2
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 0
; SigOutputVectors[0]: 2
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: main
;
;
; Vertex Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Position              0          noperspective       
; NORMAL                   0                 linear       
;
; Buffer Definitions:
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;
;
; ViewId state:
;
; Number of inputs: 0, outputs: 7, primitive outputs: 0
; Outputs dependent on ViewId: {  }
; Primitive Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
; Inputs contributing to computation of Primitive Outputs:
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

define void @main() {
  %1 = call i32 @dx.op.threadIdInGroup.i32(i32 95, i32 0)  ; ThreadIdInGroup(component)
  call void @dx.op.setMeshOutputCounts(i32 168, i32 64, i32 32)  ; SetMeshOutputCounts(numVertices,numPrimitives)
  %2 = icmp ult i32 %1, 64
  br i1 %2, label %3, label %10

; <label>:3                                       ; preds = %0
  %4 = uitofp i32 %1 to float
  %5 = fmul fast float %4, 0x3FB921FB60000000
  %6 = call float @dx.op.unary.f32(i32 12, float %5)  ; Cos(value)
  %7 = call float @dx.op.unary.f32(i32 13, float %5)  ; Sin(value)
  %8 = and i32 %1, 1
  %9 = uitofp i32 %8 to float
  call void @dx.op.storeVertexOutput.f32(i32 171, i32 0, i32 0, i8 0, float %6, i32 %1)  ; StoreVertexOutput(outputSigId,rowIndex,colIndex,value,vertexIndex)
  call void @dx.op.storeVertexOutput.f32(i32 171, i32 0, i32 0, i8 1, float %7, i32 %1)  ; StoreVertexOutput(outputSigId,rowIndex,colIndex,value,vertexIndex)
  call void @dx.op.storeVertexOutput.f32(i32 171, i32 0, i32 0, i8 2, float %9, i32 %1)  ; StoreVertexOutput(outputSigId,rowIndex,colIndex,value,vertexIndex)
  call void @dx.op.storeVertexOutput.f32(i32 171, i32 0, i32 0, i8 3, float 1.000000e+00, i32 %1)  ; StoreVertexOutput(outputSigId,rowIndex,colIndex,value,vertexIndex)
  call void @dx.op.storeVertexOutput.f32(i32 171, i32 1, i32 0, i8 0, float 0.000000e+00, i32 %1)  ; StoreVertexOutput(outputSigId,rowIndex,colIndex,value,vertexIndex)
  call void @dx.op.storeVertexOutput.f32(i32 171, i32 1, i32 0, i8 1, float 0.000000e+00, i32 %1)  ; StoreVertexOutput(outputSigId,rowIndex,colIndex,value,vertexIndex)
  call void @dx.op.storeVertexOutput.f32(i32 171, i32 1, i32 0, i8 2, float 1.000000e+00, i32 %1)  ; StoreVertexOutput(outputSigId,rowIndex,colIndex,value,vertexIndex)
  br label %10

; <label>:10                                      ; preds = %3, %0
  %11 = icmp ult i32 %1, 32
  br i1 %11, label %12, label %16

; <label>:12                                      ; preds = %10
  %13 = shl i32 %1, 1
  %14 = or i32 %13, 1
  %15 = add i32 %13, 2
  call void @dx.op.emitIndices(i32 169, i32 %1, i32 %13, i32 %14, i32 %15)  ; EmitIndices(PrimitiveIndex,VertexIndex0,VertexIndex1,VertexIndex2)
  br label %16

; <label>:16                                      ; preds = %12, %10
  ret void
}

; Function Attrs: nounwind
declare void @dx.op.emitIndices(i32, i32, i32, i32, i32) #0

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadIdInGroup.i32(i32, i32) #1

; Function Attrs: nounwind
declare void @dx.op.storeVertexOutput.f32(i32, i32, i32, i8, float, i32) #0

; Function Attrs: nounwind
declare void @dx.op.setMeshOutputCounts(i32, i32, i32) #0

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #1

attributes #0 = { nounwind }
attributes #1 = { nounwind readnone }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.viewIdState = !{!4}
!dx.entryPoints = !{!5}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 5}
!2 = !{i32 1, i32 10}
!3 = !{!"ms", i32 6, i32 5}
!4 = !{[3 x i32] [i32 0, i32 7, i32 0]}
!5 = !{void ()* @main, !"main", !6, null, !13}
!6 = !{null, !7, null}
!7 = !{!8, !11}
!8 = !{i32 0, !"SV_Position", i8 9, i8 3, !9, i8 4, i32 1, i8 4, i32 0, i8 0, !10}
!9 = !{i32 0}
!10 = !{i32 3, i32 15}
!11 = !{i32 1, !"NORMAL", i8 9, i8 0, !9, i8 2, i32 1, i8 3, i32 1, i8 0, !12}
!12 = !{i32 3, i32 7}
!13 = !{i32 9, !14}
!14 = !{!15, i32 64, i32 32, i32 2, i32 0}
!15 = !{i32 32, i32 1, i32 1}
