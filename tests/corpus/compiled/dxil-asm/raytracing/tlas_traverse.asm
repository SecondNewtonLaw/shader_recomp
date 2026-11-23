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
; shader hash: b8e34c0721331e026931ce10e08cd103
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
;       uint triangleCount;                           ; Offset:    4
;   
;   } Params;                                         ; Offset:    0 Size:     8
;
; }
;
; Resource bind info for Instances
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
; Resource bind info for Triangles
; {
;
;   struct struct.Triangle
;   {
;
;       float3 v0;                                    ; Offset:    0
;       float3 v1;                                    ; Offset:   12
;       float3 v2;                                    ; Offset:   24
;   
;   } $Element;                                       ; Offset:    0 Size:    36
;
; }
;
; Resource bind info for Output
; {
;
;   struct struct.HitResult
;   {
;
;       uint instance;                                ; Offset:    0
;       uint triangleIndex;                           ; Offset:    4
;       float t;                                      ; Offset:    8
;       uint pad;                                     ; Offset:   12
;   
;   } $Element;                                       ; Offset:    0 Size:    16
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Params                            cbuffer      NA          NA     CB0            cb0     1
; Instances                         texture  struct         r/o      T0             t0     1
; Triangles                         texture  struct         r/o      T1             t1     1
; Output                                UAV  struct         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.ResRet.i32 = type { i32, i32, i32, i32, i32 }
%"class.StructuredBuffer<InstanceBounds>" = type { %struct.InstanceBounds }
%struct.InstanceBounds = type { %struct.AABB, i32, i32, i32, i32 }
%struct.AABB = type { <3 x float>, <3 x float> }
%"class.StructuredBuffer<Triangle>" = type { %struct.Triangle }
%struct.Triangle = type { <3 x float>, <3 x float>, <3 x float> }
%"class.RWStructuredBuffer<HitResult>" = type { %struct.HitResult }
%struct.HitResult = type { i32, i32, float, i32 }
%Params = type { i32, i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %5 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %6 = uitofp i32 %5 to float
  %7 = fmul fast float %6, 0x3F847AE140000000
  %8 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %4, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %9 = extractvalue %dx.types.CBufRet.i32 %8, 0
  %10 = icmp eq i32 %9, 0
  br i1 %10, label %133, label %11

; <label>:11                                      ; preds = %0
  br label %12

; <label>:12                                      ; preds = %124, %11
  %13 = phi i32 [ %128, %124 ], [ 0, %11 ]
  %14 = phi float [ %125, %124 ], [ 0x46293E5940000000, %11 ]
  %15 = phi i32 [ %126, %124 ], [ -1, %11 ]
  %16 = phi i32 [ %127, %124 ], [ -1, %11 ]
  %17 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %3, i32 %13, i32 0)  ; BufferLoad(srv,index,wot)
  %18 = extractvalue %dx.types.ResRet.f32 %17, 0
  %19 = extractvalue %dx.types.ResRet.f32 %17, 1
  %20 = extractvalue %dx.types.ResRet.f32 %17, 2
  %21 = fsub fast float %18, %7
  %22 = fmul fast float %21, 0x7FF0000000000000
  %23 = fmul fast float %19, 0x7FF0000000000000
  %24 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %3, i32 %13, i32 12)  ; BufferLoad(srv,index,wot)
  %25 = extractvalue %dx.types.ResRet.f32 %24, 0
  %26 = extractvalue %dx.types.ResRet.f32 %24, 1
  %27 = extractvalue %dx.types.ResRet.f32 %24, 2
  %28 = fsub fast float %25, %7
  %29 = fmul fast float %28, 0x7FF0000000000000
  %30 = fmul fast float %26, 0x7FF0000000000000
  %31 = call float @dx.op.binary.f32(i32 36, float %22, float %29)  ; FMin(a,b)
  %32 = call float @dx.op.binary.f32(i32 36, float %23, float %30)  ; FMin(a,b)
  %33 = call float @dx.op.binary.f32(i32 36, float %20, float %27)  ; FMin(a,b)
  %34 = call float @dx.op.binary.f32(i32 35, float %22, float %29)  ; FMax(a,b)
  %35 = call float @dx.op.binary.f32(i32 35, float %23, float %30)  ; FMax(a,b)
  %36 = call float @dx.op.binary.f32(i32 35, float %20, float %27)  ; FMax(a,b)
  %37 = call float @dx.op.binary.f32(i32 35, float %33, float 0.000000e+00)  ; FMax(a,b)
  %38 = call float @dx.op.binary.f32(i32 35, float %31, float %32)  ; FMax(a,b)
  %39 = call float @dx.op.binary.f32(i32 35, float %38, float %37)  ; FMax(a,b)
  %40 = call float @dx.op.binary.f32(i32 36, float %36, float 0x46293E5940000000)  ; FMin(a,b)
  %41 = call float @dx.op.binary.f32(i32 36, float %34, float %35)  ; FMin(a,b)
  %42 = call float @dx.op.binary.f32(i32 36, float %41, float %40)  ; FMin(a,b)
  %43 = fcmp fast ult float %42, %39
  br i1 %43, label %124, label %44

; <label>:44                                      ; preds = %12
  %45 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %4, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %46 = extractvalue %dx.types.CBufRet.i32 %45, 1
  %47 = icmp eq i32 %46, 0
  br i1 %47, label %124, label %48

; <label>:48                                      ; preds = %44
  br label %49

; <label>:49                                      ; preds = %115, %48
  %50 = phi i32 [ %119, %115 ], [ 0, %48 ]
  %51 = phi float [ %116, %115 ], [ %14, %48 ]
  %52 = phi i32 [ %117, %115 ], [ %15, %48 ]
  %53 = phi i32 [ %118, %115 ], [ %16, %48 ]
  %54 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %50, i32 0)  ; BufferLoad(srv,index,wot)
  %55 = extractvalue %dx.types.ResRet.f32 %54, 0
  %56 = extractvalue %dx.types.ResRet.f32 %54, 1
  %57 = extractvalue %dx.types.ResRet.f32 %54, 2
  %58 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %50, i32 12)  ; BufferLoad(srv,index,wot)
  %59 = extractvalue %dx.types.ResRet.f32 %58, 0
  %60 = extractvalue %dx.types.ResRet.f32 %58, 1
  %61 = extractvalue %dx.types.ResRet.f32 %58, 2
  %62 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %50, i32 24)  ; BufferLoad(srv,index,wot)
  %63 = extractvalue %dx.types.ResRet.f32 %62, 0
  %64 = extractvalue %dx.types.ResRet.f32 %62, 1
  %65 = extractvalue %dx.types.ResRet.f32 %62, 2
  %66 = fsub fast float %59, %55
  %67 = fsub fast float %60, %56
  %68 = fsub fast float %61, %57
  %69 = fsub fast float %63, %55
  %70 = fsub fast float %64, %56
  %71 = fsub fast float %65, %57
  %72 = fsub fast float -0.000000e+00, %70
  %73 = call float @dx.op.dot3.f32(i32 55, float %66, float %67, float %68, float %72, float %69, float 0.000000e+00)  ; Dot3(ax,ay,az,bx,by,bz)
  %74 = call float @dx.op.unary.f32(i32 6, float %73)  ; FAbs(value)
  %75 = fcmp fast olt float %74, 0x3E45798EE0000000
  br i1 %75, label %107, label %76

; <label>:76                                      ; preds = %49
  %77 = fdiv fast float 1.000000e+00, %73
  %78 = fsub fast float %7, %55
  %79 = fsub fast float -0.000000e+00, %56
  %80 = fsub fast float -0.000000e+00, %57
  %81 = call float @dx.op.dot3.f32(i32 55, float %78, float %79, float %80, float %72, float %69, float 0.000000e+00)  ; Dot3(ax,ay,az,bx,by,bz)
  %82 = fmul fast float %81, %77
  %83 = fcmp fast olt float %82, 0.000000e+00
  %84 = fcmp fast ogt float %82, 1.000000e+00
  %85 = or i1 %83, %84
  br i1 %85, label %107, label %86

; <label>:86                                      ; preds = %76
  %87 = fmul fast float %68, %79
  %88 = fmul fast float %67, %80
  %89 = fsub fast float %87, %88
  %90 = fmul fast float %66, %80
  %91 = fmul fast float %68, %78
  %92 = fsub fast float %90, %91
  %93 = fmul fast float %67, %78
  %94 = fmul fast float %66, %79
  %95 = fsub fast float %93, %94
  %96 = call float @dx.op.dot3.f32(i32 55, float 0.000000e+00, float 0.000000e+00, float 1.000000e+00, float %89, float %92, float %95)  ; Dot3(ax,ay,az,bx,by,bz)
  %97 = fmul fast float %96, %77
  %98 = fcmp fast olt float %97, 0.000000e+00
  %99 = fadd fast float %96, %81
  %100 = fmul fast float %99, %77
  %101 = fcmp fast ogt float %100, 1.000000e+00
  %102 = or i1 %98, %101
  br i1 %102, label %107, label %103

; <label>:103                                     ; preds = %86
  %104 = call float @dx.op.dot3.f32(i32 55, float %69, float %70, float %71, float %89, float %92, float %95)  ; Dot3(ax,ay,az,bx,by,bz)
  %105 = fmul fast float %104, %77
  %106 = fcmp fast ogt float %105, 0.000000e+00
  br label %107

; <label>:107                                     ; preds = %103, %86, %76, %49
  %108 = phi float [ %105, %103 ], [ 0.000000e+00, %49 ], [ 0.000000e+00, %76 ], [ 0.000000e+00, %86 ]
  %109 = phi i1 [ %106, %103 ], [ false, %49 ], [ false, %76 ], [ false, %86 ]
  %110 = fcmp fast olt float %108, %51
  %111 = and i1 %109, %110
  br i1 %111, label %112, label %115

; <label>:112                                     ; preds = %107
  %113 = call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %dx.types.Handle %3, i32 %13, i32 24)  ; BufferLoad(srv,index,wot)
  %114 = extractvalue %dx.types.ResRet.i32 %113, 0
  br label %115

; <label>:115                                     ; preds = %112, %107
  %116 = phi float [ %108, %112 ], [ %51, %107 ]
  %117 = phi i32 [ %50, %112 ], [ %52, %107 ]
  %118 = phi i32 [ %114, %112 ], [ %53, %107 ]
  %119 = add i32 %50, 1
  %120 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %4, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %121 = extractvalue %dx.types.CBufRet.i32 %120, 1
  %122 = icmp ult i32 %119, %121
  br i1 %122, label %49, label %123

; <label>:123                                     ; preds = %115
  br label %124

; <label>:124                                     ; preds = %123, %44, %12
  %125 = phi float [ %14, %12 ], [ %14, %44 ], [ %116, %123 ]
  %126 = phi i32 [ %15, %12 ], [ %15, %44 ], [ %117, %123 ]
  %127 = phi i32 [ %16, %12 ], [ %16, %44 ], [ %118, %123 ]
  %128 = add i32 %13, 1
  %129 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %4, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %130 = extractvalue %dx.types.CBufRet.i32 %129, 0
  %131 = icmp ult i32 %128, %130
  br i1 %131, label %12, label %132

; <label>:132                                     ; preds = %124
  br label %133

; <label>:133                                     ; preds = %132, %0
  %134 = phi float [ 0x46293E5940000000, %0 ], [ %125, %132 ]
  %135 = phi i32 [ -1, %0 ], [ %126, %132 ]
  %136 = phi i32 [ -1, %0 ], [ %127, %132 ]
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %5, i32 0, i32 %136, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %5, i32 4, i32 %135, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %5, i32 8, float %134, float undef, float undef, float undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %5, i32 12, i32 0, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readnone
declare float @dx.op.binary.f32(i32, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32, %dx.types.Handle, i32, i32) #1

; Function Attrs: nounwind
declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8) #2

; Function Attrs: nounwind
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8) #2

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind readonly }
attributes #2 = { nounwind }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!15}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{!5, !10, !13, null}
!5 = !{!6, !8}
!6 = !{i32 0, %"class.StructuredBuffer<InstanceBounds>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 40}
!8 = !{i32 1, %"class.StructuredBuffer<Triangle>"* undef, !"", i32 0, i32 1, i32 1, i32 12, i32 0, !9}
!9 = !{i32 1, i32 36}
!10 = !{!11}
!11 = !{i32 0, %"class.RWStructuredBuffer<HitResult>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !12}
!12 = !{i32 1, i32 16}
!13 = !{!14}
!14 = !{i32 0, %Params* undef, !"", i32 0, i32 0, i32 1, i32 8, null}
!15 = !{void ()* @main, !"main", null, !4, !16}
!16 = !{i32 0, i64 16, i32 4, !17}
!17 = !{i32 64, i32 1, i32 1}
