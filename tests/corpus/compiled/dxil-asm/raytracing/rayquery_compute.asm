;
; Note: shader requires additional functionality:
;       Raytracing tier 1.1 features
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
; shader hash: 820f48f38a2a945cd59bacd10f255d0a
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
; Resource bind info for Hits
; {
;
;   uint $Element;                                    ; Offset:    0 Size:     4
;
; }
;
; Resource bind info for Depths
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
; Scene                             texture     i32         ras      T0             t0     1
; Hits                                  UAV  struct         r/w      U0             u0     1
; Depths                                UAV  struct         r/w      U1             u1     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResBind = type { i32, i32, i32, i8 }
%dx.types.ResourceProperties = type { i32, i32 }
%struct.RaytracingAccelerationStructure = type { i32 }
%"class.RWStructuredBuffer<unsigned int>" = type { i32 }
%"class.RWStructuredBuffer<float>" = type { float }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 1, i32 1, i32 0, i8 1 }, i32 1, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind { i32 0, i32 0, i32 0, i8 1 }, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandleFromBinding(i32 217, %dx.types.ResBind zeroinitializer, i32 0, i1 false)  ; CreateHandleFromBinding(bind,index,nonUniformIndex)
  %4 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %5 = uitofp i32 %4 to float
  %6 = fmul fast float %5, 0x3F847AE140000000
  %7 = call i32 @dx.op.allocateRayQuery(i32 178, i32 0)  ; AllocateRayQuery(constRayFlags)
  %8 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %3, %dx.types.ResourceProperties { i32 16, i32 0 })  ; AnnotateHandle(res,props)  resource: RTAccelerationStructure
  call void @dx.op.rayQuery_TraceRayInline(i32 179, i32 %7, %dx.types.Handle %8, i32 0, i32 255, float %6, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 1.000000e+00, float 1.000000e+09)  ; RayQuery_TraceRayInline(rayQueryHandle,accelerationStructure,rayFlags,instanceInclusionMask,origin_X,origin_Y,origin_Z,tMin,direction_X,direction_Y,direction_Z,tMax)
  %9 = call i1 @dx.op.rayQuery_Proceed.i1(i32 180, i32 %7)  ; RayQuery_Proceed(rayQueryHandle)
  br i1 %9, label %10, label %18

; <label>:10                                      ; preds = %0
  br label %11

; <label>:11                                      ; preds = %15, %10
  %12 = call i32 @dx.op.rayQuery_StateScalar.i32(i32 185, i32 %7)  ; RayQuery_CandidateType(rayQueryHandle)
  %13 = icmp eq i32 %12, 0
  br i1 %13, label %14, label %15

; <label>:14                                      ; preds = %11
  call void @dx.op.rayQuery_CommitNonOpaqueTriangleHit(i32 182, i32 %7)  ; RayQuery_CommitNonOpaqueTriangleHit(rayQueryHandle)
  br label %15

; <label>:15                                      ; preds = %14, %11
  %16 = call i1 @dx.op.rayQuery_Proceed.i1(i32 180, i32 %7)  ; RayQuery_Proceed(rayQueryHandle)
  br i1 %16, label %11, label %17

; <label>:17                                      ; preds = %15
  br label %18

; <label>:18                                      ; preds = %17, %0
  %19 = call i32 @dx.op.rayQuery_StateScalar.i32(i32 184, i32 %7)  ; RayQuery_CommittedStatus(rayQueryHandle)
  %20 = icmp eq i32 %19, 1
  %21 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %2, %dx.types.ResourceProperties { i32 4108, i32 4 })  ; AnnotateHandle(res,props)  resource: RWStructuredBuffer<stride=4>
  %22 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %1, %dx.types.ResourceProperties { i32 4108, i32 4 })  ; AnnotateHandle(res,props)  resource: RWStructuredBuffer<stride=4>
  br i1 %20, label %23, label %25

; <label>:23                                      ; preds = %18
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %21, i32 %4, i32 0, i32 1, i32 undef, i32 undef, i32 undef, i8 1, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  %24 = call float @dx.op.rayQuery_StateScalar.f32(i32 200, i32 %7)  ; RayQuery_CommittedRayT(rayQueryHandle)
  call void @dx.op.rawBufferStore.f32(i32 140, %dx.types.Handle %22, i32 %4, i32 0, float %24, float undef, float undef, float undef, i8 1, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  br label %26

; <label>:25                                      ; preds = %18
  call void @dx.op.rawBufferStore.i32(i32 140, %dx.types.Handle %21, i32 %4, i32 0, i32 0, i32 undef, i32 undef, i32 undef, i8 1, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  call void @dx.op.rawBufferStore.f32(i32 140, %dx.types.Handle %22, i32 %4, i32 0, float 1.000000e+09, float undef, float undef, float undef, i8 1, i32 4)  ; RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)
  br label %26

; <label>:26                                      ; preds = %25, %23
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind
declare void @dx.op.rawBufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8, i32) #1

; Function Attrs: nounwind
declare void @dx.op.rawBufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8, i32) #1

; Function Attrs: nounwind
declare i32 @dx.op.allocateRayQuery(i32, i32) #1

; Function Attrs: nounwind
declare void @dx.op.rayQuery_TraceRayInline(i32, i32, %dx.types.Handle, i32, i32, float, float, float, float, float, float, float, float) #1

; Function Attrs: nounwind readonly
declare float @dx.op.rayQuery_StateScalar.f32(i32, i32) #2

; Function Attrs: nounwind readonly
declare i32 @dx.op.rayQuery_StateScalar.i32(i32, i32) #2

; Function Attrs: nounwind
declare void @dx.op.rayQuery_CommitNonOpaqueTriangleHit(i32, i32) #1

; Function Attrs: nounwind
declare i1 @dx.op.rayQuery_Proceed.i1(i32, i32) #1

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties) #0

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.createHandleFromBinding(i32, %dx.types.ResBind, i32, i1) #0

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
attributes #2 = { nounwind readonly }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!12}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 9}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 9}
!4 = !{!5, !8, null, null}
!5 = !{!6}
!6 = !{i32 0, %struct.RaytracingAccelerationStructure* undef, !"", i32 0, i32 0, i32 1, i32 16, i32 0, !7}
!7 = !{i32 0, i32 4}
!8 = !{!9, !11}
!9 = !{i32 0, %"class.RWStructuredBuffer<unsigned int>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !10}
!10 = !{i32 1, i32 4}
!11 = !{i32 1, %"class.RWStructuredBuffer<float>"* undef, !"", i32 0, i32 1, i32 1, i32 12, i1 false, i1 false, i1 false, !10}
!12 = !{void ()* @main, !"main", null, !4, !13}
!13 = !{i32 0, i64 8623489040, i32 4, !14}
!14 = !{i32 64, i32 1, i32 1}
