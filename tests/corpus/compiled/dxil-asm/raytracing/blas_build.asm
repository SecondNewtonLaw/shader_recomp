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
; shader hash: 38845714a6d8a4b827ff2fabe4c7956c
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
;       uint triangleCount;                           ; Offset:    0
;   
;   } Params;                                         ; Offset:    0 Size:     4
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
; Resource bind info for Leaves
; {
;
;   struct struct.Leaf
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
;       uint triangleIndex;                           ; Offset:   24
;       uint morton;                                  ; Offset:   28
;       uint pad;                                     ; Offset:   32
;   
;   } $Element;                                       ; Offset:    0 Size:    36
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
; Triangles                         texture  struct         r/o      T0             t0     1
; Leaves                                UAV  struct         r/w      U0             u0     1
; Counters                              UAV  struct         r/w      U1             u1     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.StructuredBuffer<Triangle>" = type { %struct.Triangle }
%struct.Triangle = type { <3 x float>, <3 x float>, <3 x float> }
%"class.RWStructuredBuffer<Leaf>" = type { %struct.Leaf }
%struct.Leaf = type { %struct.AABB, i32, i32, i32 }
%struct.AABB = type { <3 x float>, <3 x float> }
%"class.RWStructuredBuffer<unsigned int>" = type { i32 }
%Params = type { i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 1, i32 1, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %4 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %5 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %6 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %4, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %7 = extractvalue %dx.types.CBufRet.i32 %6, 0
  %8 = icmp ult i32 %5, %7
  br i1 %8, label %9, label %82

; <label>:9                                       ; preds = %0
  %10 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %3, i32 %5, i32 0)  ; BufferLoad(srv,index,wot)
  %11 = extractvalue %dx.types.ResRet.f32 %10, 0
  %12 = extractvalue %dx.types.ResRet.f32 %10, 1
  %13 = extractvalue %dx.types.ResRet.f32 %10, 2
  %14 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %3, i32 %5, i32 12)  ; BufferLoad(srv,index,wot)
  %15 = extractvalue %dx.types.ResRet.f32 %14, 0
  %16 = extractvalue %dx.types.ResRet.f32 %14, 1
  %17 = extractvalue %dx.types.ResRet.f32 %14, 2
  %18 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %3, i32 %5, i32 24)  ; BufferLoad(srv,index,wot)
  %19 = extractvalue %dx.types.ResRet.f32 %18, 0
  %20 = extractvalue %dx.types.ResRet.f32 %18, 1
  %21 = extractvalue %dx.types.ResRet.f32 %18, 2
  %22 = call float @dx.op.binary.f32(i32 36, float %15, float %19)  ; FMin(a,b)
  %23 = call float @dx.op.binary.f32(i32 36, float %16, float %20)  ; FMin(a,b)
  %24 = call float @dx.op.binary.f32(i32 36, float %17, float %21)  ; FMin(a,b)
  %25 = call float @dx.op.binary.f32(i32 36, float %11, float %22)  ; FMin(a,b)
  %26 = call float @dx.op.binary.f32(i32 36, float %12, float %23)  ; FMin(a,b)
  %27 = call float @dx.op.binary.f32(i32 36, float %13, float %24)  ; FMin(a,b)
  %28 = call float @dx.op.binary.f32(i32 35, float %15, float %19)  ; FMax(a,b)
  %29 = call float @dx.op.binary.f32(i32 35, float %16, float %20)  ; FMax(a,b)
  %30 = call float @dx.op.binary.f32(i32 35, float %17, float %21)  ; FMax(a,b)
  %31 = call float @dx.op.binary.f32(i32 35, float %11, float %28)  ; FMax(a,b)
  %32 = call float @dx.op.binary.f32(i32 35, float %12, float %29)  ; FMax(a,b)
  %33 = call float @dx.op.binary.f32(i32 35, float %13, float %30)  ; FMax(a,b)
  %34 = fadd fast float %15, %11
  %35 = fadd fast float %16, %12
  %36 = fadd fast float %17, %13
  %37 = fadd fast float %34, %19
  %38 = fadd fast float %35, %20
  %39 = fadd fast float %36, %21
  %40 = fmul fast float %37, 3.410000e+02
  %41 = fmul fast float %38, 3.410000e+02
  %42 = fmul fast float %39, 3.410000e+02
  %43 = fadd fast float %40, 5.000000e-01
  %44 = fadd fast float %41, 5.000000e-01
  %45 = fadd fast float %42, 5.000000e-01
  %46 = call float @dx.op.binary.f32(i32 35, float %43, float 0.000000e+00)  ; FMax(a,b)
  %47 = call float @dx.op.binary.f32(i32 35, float %44, float 0.000000e+00)  ; FMax(a,b)
  %48 = call float @dx.op.binary.f32(i32 35, float %45, float 0.000000e+00)  ; FMax(a,b)
  %49 = call float @dx.op.binary.f32(i32 36, float %46, float 1.023000e+03)  ; FMin(a,b)
  %50 = call float @dx.op.binary.f32(i32 36, float %47, float 1.023000e+03)  ; FMin(a,b)
  %51 = call float @dx.op.binary.f32(i32 36, float %48, float 1.023000e+03)  ; FMin(a,b)
  %52 = fptoui float %49 to i32
  %53 = fptoui float %50 to i32
  %54 = fptoui float %51 to i32
  %55 = mul i32 %52, 65537
  %56 = and i32 %55, -16776961
  %57 = mul i32 %56, 257
  %58 = and i32 %57, 251719695
  %59 = mul nuw i32 %58, 17
  %60 = and i32 %59, -1022611261
  %61 = mul i32 %60, 5
  %62 = and i32 %61, 1227133513
  %63 = mul i32 %53, 65537
  %64 = and i32 %63, -16776961
  %65 = mul i32 %64, 257
  %66 = and i32 %65, 251719695
  %67 = mul nuw i32 %66, 17
  %68 = and i32 %67, -1022611261
  %69 = mul i32 %68, 10
  %70 = and i32 %69, -1840700270
  %71 = or i32 %70, %62
  %72 = mul i32 %54, 65537
  %73 = and i32 %72, -16776961
  %74 = mul i32 %73, 257
  %75 = and i32 %74, 251719695
  %76 = mul nuw i32 %75, 17
  %77 = and i32 %76, -1022611261
  %78 = mul i32 %77, 20
  %79 = and i32 %78, 613566756
  %80 = or i32 %71, %79
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %2, i32 %5, i32 0, float %25, float %26, float %27, float undef, i8 7)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %2, i32 %5, i32 12, float %31, float %32, float %33, float undef, i8 7)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %5, i32 24, i32 %5, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %5, i32 28, i32 %80, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %2, i32 %5, i32 32, i32 0, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  %81 = call i32 @dx.op.atomicBinOp.i32(i32 78, %dx.types.Handle %1, i32 0, i32 0, i32 0, i32 undef, i32 1)  ; AtomicBinOp(handle,atomicOp,offset0,offset1,offset2,newValue)
  br label %82

; <label>:82                                      ; preds = %9, %0
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

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #2

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
!dx.entryPoints = !{!14}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{!5, !8, !12, null}
!5 = !{!6}
!6 = !{i32 0, %"class.StructuredBuffer<Triangle>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 36}
!8 = !{!9, !10}
!9 = !{i32 0, %"class.RWStructuredBuffer<Leaf>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !7}
!10 = !{i32 1, %"class.RWStructuredBuffer<unsigned int>"* undef, !"", i32 0, i32 1, i32 1, i32 12, i1 false, i1 false, i1 false, !11}
!11 = !{i32 1, i32 4}
!12 = !{!13}
!13 = !{i32 0, %Params* undef, !"", i32 0, i32 0, i32 1, i32 4, null}
!14 = !{void ()* @main, !"main", null, !4, !15}
!15 = !{i32 0, i64 16, i32 4, !16}
!16 = !{i32 64, i32 1, i32 1}
