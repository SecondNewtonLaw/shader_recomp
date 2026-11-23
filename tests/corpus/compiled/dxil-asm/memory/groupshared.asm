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
; shader hash: 0d828fa5cccbb3ee05bcfdd1fc46f09c
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Compute Shader
; NumThreads=(16,16,1)
; NumBytesGroupSharedMemory: 1088
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
;   float4 $Element;                                  ; Offset:    0 Size:    16
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Input                             texture     f32          2d      T0             t0     1
; Output                                UAV  struct         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.Texture2D<vector<float, 4> >" = type { <4 x float>, %"class.Texture2D<vector<float, 4> >::mips_type" }
%"class.Texture2D<vector<float, 4> >::mips_type" = type { i32 }
%"class.RWStructuredBuffer<vector<float, 4> >" = type { <4 x float> }

@"\01?tile@@3PAY0BB@$$CAMA.1dim" = addrspace(3) global [272 x float] undef, align 4

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call i32 @dx.op.groupId.i32(i32 94, i32 0)  ; GroupId(component)
  %4 = call i32 @dx.op.groupId.i32(i32 94, i32 1)  ; GroupId(component)
  %5 = call i32 @dx.op.threadIdInGroup.i32(i32 95, i32 0)  ; ThreadIdInGroup(component)
  %6 = call i32 @dx.op.threadIdInGroup.i32(i32 95, i32 1)  ; ThreadIdInGroup(component)
  %7 = shl i32 %3, 4
  %8 = shl i32 %4, 4
  %9 = add i32 %5, %7
  %10 = add i32 %6, %8
  %11 = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %2, i32 0, i32 %9, i32 %10, i32 undef, i32 undef, i32 undef, i32 undef)  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)
  %12 = extractvalue %dx.types.ResRet.f32 %11, 0
  %13 = extractvalue %dx.types.ResRet.f32 %11, 1
  %14 = extractvalue %dx.types.ResRet.f32 %11, 2
  %15 = extractvalue %dx.types.ResRet.f32 %11, 3
  %16 = fadd fast float %12, %13
  %17 = mul i32 %6, 17
  %18 = add i32 %5, %17
  %19 = getelementptr [272 x float], [272 x float] addrspace(3)* @"\01?tile@@3PAY0BB@$$CAMA.1dim", i32 0, i32 %18
  store float %16, float addrspace(3)* %19, align 4, !tbaa !14
  call void @dx.op.barrier(i32 80, i32 9)  ; Barrier(barrierMode)
  %20 = mul i32 %5, 17
  %21 = add i32 %6, %20
  %22 = getelementptr [272 x float], [272 x float] addrspace(3)* @"\01?tile@@3PAY0BB@$$CAMA.1dim", i32 0, i32 %21
  %23 = load float, float addrspace(3)* %22, align 4, !tbaa !14
  %24 = sub i32 15, %5
  %25 = sub i32 15, %6
  %26 = mul i32 %25, 17
  %27 = add i32 %24, %26
  %28 = getelementptr [272 x float], [272 x float] addrspace(3)* @"\01?tile@@3PAY0BB@$$CAMA.1dim", i32 0, i32 %27
  %29 = load float, float addrspace(3)* %28, align 4, !tbaa !14
  %30 = fadd fast float %29, %23
  %31 = shl i32 %10, 6
  %32 = add i32 %31, %9
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %32, i32 0, float %30, float %14, float %15, float 1.000000e+00, i8 15)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.groupId.i32(i32, i32) #0

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadIdInGroup.i32(i32, i32) #0

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32) #1

; Function Attrs: noduplicate nounwind
declare void @dx.op.barrier(i32, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

; Function Attrs: nounwind
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8) #3

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind readonly }
attributes #2 = { noduplicate nounwind }
attributes #3 = { nounwind }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!11}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{!5, !8, null, null}
!5 = !{!6}
!6 = !{i32 0, %"class.Texture2D<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i32 0, !7}
!7 = !{i32 0, i32 9}
!8 = !{!9}
!9 = !{i32 0, %"class.RWStructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !10}
!10 = !{i32 1, i32 16}
!11 = !{void ()* @main, !"main", null, !4, !12}
!12 = !{i32 0, i64 16, i32 4, !13}
!13 = !{i32 16, i32 16, i32 1}
!14 = !{!15, !15, i64 0}
!15 = !{!"float", !16, i64 0}
!16 = !{!"omnipotent char", !17, i64 0}
!17 = !{!"Simple C/C++ TBAA"}
