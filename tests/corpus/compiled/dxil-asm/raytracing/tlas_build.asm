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
; shader hash: 725ac47c3c08f8d7727f2344682de5f0
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
; cbuffer Params
; {
;
;   struct Params
;   {
;
;       uint instanceCount;                           ; Offset:    0
;   
;   } Params;                                         ; Offset:    0 Size:     4
;
; }
;
; Resource bind info for BlasBounds
; {
;
;   struct struct.AABB
;   {
;
;       float3 minimum;                               ; Offset:    0
;       float3 maximum;                               ; Offset:   12
;   
;   } $Element;                                       ; Offset:    0 Size:    24
;
; }
;
; Resource bind info for Instances
; {
;
;   struct hostlayout.struct.Instance
;   {
;
;       column_major float4x4 transform;              ; Offset:    0
;       uint blasOffset;                              ; Offset:   64
;       uint blasCount;                               ; Offset:   68
;       uint instanceId;                              ; Offset:   72
;       uint mask;                                    ; Offset:   76
;   
;   } $Element;                                       ; Offset:    0 Size:    80
;
; }
;
; Resource bind info for Output
; {
;
;   struct struct.InstanceBounds
;   {
;
;       struct struct.AABB
;       {
;
;           float3 minimum;                           ; Offset:    0
;           float3 maximum;                           ; Offset:   12
;       
;       } bounds;                                     ; Offset:    0
;
;       uint instance;                                ; Offset:   24
;       uint pad0;                                    ; Offset:   28
;       uint pad1;                                    ; Offset:   32
;       uint pad2;                                    ; Offset:   36
;   
;   } $Element;                                       ; Offset:    0 Size:    40
;
; }
;
; Resource bind info for Counters
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
; Params                            cbuffer      NA          NA     CB0            cb0     1
; BlasBounds                        texture  struct         r/o      T0             t0     1
; Instances                         texture  struct         r/o      T1             t1     1
; Output                                UAV  struct         r/w      U0             u0     1
; Counters                              UAV  struct         r/w      U1             u1     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%"class.StructuredBuffer<AABB>" = type { %struct.AABB }
%struct.AABB = type { <3 x float>, <3 x float> }
%"hostlayout.class.StructuredBuffer<Instance>" = type { %hostlayout.struct.Instance }
%hostlayout.struct.Instance = type { [4 x <4 x float>], i32, i32, i32, i32 }
%"class.RWStructuredBuffer<InstanceBounds>" = type { %struct.InstanceBounds }
%struct.InstanceBounds = type { %struct.AABB, i32, i32, i32, i32 }
%"class.RWStructuredBuffer<unsigned int>" = type { i32 }
%Params = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %5 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %6 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %7 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %5, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %8 = extractvalue %dx.types.CBufRet.i32 %7, 0
  %9 = icmp ult i32 %6, %8
  br i1 %9, label %10, label %100

; <label>:10                                      ; preds = %0
  %11 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %3, i32 %6, i32 0)  ; BufferLoad(srv,index,wot)
  %12 = extractvalue %dx.types.ResRet.f32 %11, 0
  %13 = extractvalue %dx.types.ResRet.f32 %11, 1
  %14 = extractvalue %dx.types.ResRet.f32 %11, 2
  %15 = extractvalue %dx.types.ResRet.f32 %11, 3
  %16 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %3, i32 %6, i32 16)  ; BufferLoad(srv,index,wot)
  %17 = extractvalue %dx.types.ResRet.f32 %16, 0
  %18 = extractvalue %dx.types.ResRet.f32 %16, 1
  %19 = extractvalue %dx.types.ResRet.f32 %16, 2
  %20 = extractvalue %dx.types.ResRet.f32 %16, 3
  %21 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %3, i32 %6, i32 32)  ; BufferLoad(srv,index,wot)
  %22 = extractvalue %dx.types.ResRet.f32 %21, 0
  %23 = extractvalue %dx.types.ResRet.f32 %21, 1
  %24 = extractvalue %dx.types.ResRet.f32 %21, 2
  %25 = extractvalue %dx.types.ResRet.f32 %21, 3
  %26 = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %3, i32 %6, i32 64)  ; BufferLoad(srv,index,wot)
  %27 = extractvalue %dx.types.ResRet.i32 %26, 0
  %28 = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %3, i32 %6, i32 68)  ; BufferLoad(srv,index,wot)
  %29 = extractvalue %dx.types.ResRet.i32 %28, 0
  %30 = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %3, i32 %6, i32 72)  ; BufferLoad(srv,index,wot)
  %31 = extractvalue %dx.types.ResRet.i32 %30, 0
  %32 = icmp eq i32 %29, 0
  br i1 %32, label %92, label %33

; <label>:33                                      ; preds = %10
  br label %34

; <label>:34                                      ; preds = %88, %33
  %35 = phi i32 [ %89, %88 ], [ 0, %33 ]
  %36 = phi float [ %83, %88 ], [ 0xC6293E5940000000, %33 ]
  %37 = phi float [ %84, %88 ], [ 0xC6293E5940000000, %33 ]
  %38 = phi float [ %85, %88 ], [ 0xC6293E5940000000, %33 ]
  %39 = phi float [ %80, %88 ], [ 0x46293E5940000000, %33 ]
  %40 = phi float [ %81, %88 ], [ 0x46293E5940000000, %33 ]
  %41 = phi float [ %82, %88 ], [ 0x46293E5940000000, %33 ]
  %42 = add i32 %35, %27
  %43 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %4, i32 %42, i32 0)  ; BufferLoad(srv,index,wot)
  %44 = extractvalue %dx.types.ResRet.f32 %43, 0
  %45 = extractvalue %dx.types.ResRet.f32 %43, 1
  %46 = extractvalue %dx.types.ResRet.f32 %43, 2
  %47 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %4, i32 %42, i32 12)  ; BufferLoad(srv,index,wot)
  %48 = extractvalue %dx.types.ResRet.f32 %47, 0
  %49 = extractvalue %dx.types.ResRet.f32 %47, 1
  %50 = extractvalue %dx.types.ResRet.f32 %47, 2
  br label %51

; <label>:51                                      ; preds = %51, %34
  %52 = phi i32 [ 0, %34 ], [ %86, %51 ]
  %53 = phi float [ %36, %34 ], [ %83, %51 ]
  %54 = phi float [ %37, %34 ], [ %84, %51 ]
  %55 = phi float [ %38, %34 ], [ %85, %51 ]
  %56 = phi float [ %39, %34 ], [ %80, %51 ]
  %57 = phi float [ %40, %34 ], [ %81, %51 ]
  %58 = phi float [ %41, %34 ], [ %82, %51 ]
  %59 = and i32 %52, 1
  %60 = icmp ne i32 %59, 0
  %61 = select i1 %60, float %48, float %44
  %62 = and i32 %52, 2
  %63 = icmp ne i32 %62, 0
  %64 = select i1 %63, float %49, float %45
  %65 = and i32 %52, 4
  %66 = icmp ne i32 %65, 0
  %67 = select i1 %66, float %50, float %46
  %68 = fmul fast float %61, %12
  %69 = call float @dx.op.tertiary.f32(i32 46, float %64, float %13, float %68)  ; FMad(a,b,c)
  %70 = call float @dx.op.tertiary.f32(i32 46, float %67, float %14, float %69)  ; FMad(a,b,c)
  %71 = fadd fast float %70, %15
  %72 = fmul fast float %61, %17
  %73 = call float @dx.op.tertiary.f32(i32 46, float %64, float %18, float %72)  ; FMad(a,b,c)
  %74 = call float @dx.op.tertiary.f32(i32 46, float %67, float %19, float %73)  ; FMad(a,b,c)
  %75 = fadd fast float %74, %20
  %76 = fmul fast float %61, %22
  %77 = call float @dx.op.tertiary.f32(i32 46, float %64, float %23, float %76)  ; FMad(a,b,c)
  %78 = call float @dx.op.tertiary.f32(i32 46, float %67, float %24, float %77)  ; FMad(a,b,c)
  %79 = fadd fast float %78, %25
  %80 = call float @dx.op.binary.f32(i32 36, float %56, float %71)  ; FMin(a,b)
  %81 = call float @dx.op.binary.f32(i32 36, float %57, float %75)  ; FMin(a,b)
  %82 = call float @dx.op.binary.f32(i32 36, float %58, float %79)  ; FMin(a,b)
  %83 = call float @dx.op.binary.f32(i32 35, float %53, float %71)  ; FMax(a,b)
  %84 = call float @dx.op.binary.f32(i32 35, float %54, float %75)  ; FMax(a,b)
  %85 = call float @dx.op.binary.f32(i32 35, float %55, float %79)  ; FMax(a,b)
  %86 = add nuw nsw i32 %52, 1
  %87 = icmp eq i32 %86, 8
  br i1 %87, label %88, label %51

; <label>:88                                      ; preds = %51
  %89 = add nuw i32 %35, 1
  %90 = icmp eq i32 %89, %29
  br i1 %90, label %91, label %34

; <label>:91                                      ; preds = %88
  br label %92

; <label>:92                                      ; preds = %91, %10
  %93 = phi float [ 0xC6293E5940000000, %10 ], [ %83, %91 ]
  %94 = phi float [ 0xC6293E5940000000, %10 ], [ %84, %91 ]
  %95 = phi float [ 0xC6293E5940000000, %10 ], [ %85, %91 ]
  %96 = phi float [ 0x46293E5940000000, %10 ], [ %80, %91 ]
  %97 = phi float [ 0x46293E5940000000, %10 ], [ %81, %91 ]
  %98 = phi float [ 0x46293E5940000000, %10 ], [ %82, %91 ]
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %2, i32 %6, i32 0, float %96, float %97, float %98, float undef, i8 7)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %2, i32 %6, i32 12, float %93, float %94, float %95, float undef, i8 7)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %6, i32 24, i32 %31, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %6, i32 28, i32 0, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %6, i32 32, i32 0, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %6, i32 36, i32 0, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %99 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 0, i32 0, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %100

; <label>:100                                     ; preds = %92, %0
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind
declare i32 @dx.op.atomicBinOp.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32, %dx.types.Handle, i32, i32) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32) #2

; Function Attrs: nounwind
declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8) #1

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
!dx.entryPoints = !{!17}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{!5, !10, !15, null}
!5 = !{!6, !8}
!6 = !{i32 0, %"class.StructuredBuffer<AABB>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 24}
!8 = !{i32 1, %"hostlayout.class.StructuredBuffer<Instance>"* undef, !"", i32 0, i32 1, i32 1, i32 12, i32 0, !9}
!9 = !{i32 1, i32 80}
!10 = !{!11, !13}
!11 = !{i32 0, %"class.RWStructuredBuffer<InstanceBounds>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !12}
!12 = !{i32 1, i32 40}
!13 = !{i32 1, %"class.RWStructuredBuffer<unsigned int>"* undef, !"", i32 0, i32 1, i32 1, i32 12, i1 false, i1 false, i1 false, !14}
!14 = !{i32 1, i32 4}
!15 = !{!16}
!16 = !{i32 0, %Params* undef, !"", i32 0, i32 0, i32 1, i32 4, null}
!17 = !{void ()* @main, !"main", null, !4, !18}
!18 = !{i32 0, i64 16, i32 4, !19}
!19 = !{i32 64, i32 1, i32 1}
