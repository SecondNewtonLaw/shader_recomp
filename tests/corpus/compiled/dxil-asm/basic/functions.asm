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
; shader hash: bb293a459633b2767d9efa42d6321cd5
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
; Resource bind info for Input
; {
;
;   float4 $Element;                                  ; Offset:    0 Size:    16
;
; }
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
; Input                             texture  struct         r/o      T0             t0     1
; Output                                UAV  struct         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%"class.StructuredBuffer<vector<float, 4> >" = type { <4 x float> }
%"class.RWStructuredBuffer<vector<float, 4> >" = type { <4 x float> }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %4 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %3, i32 0)  ; BufferLoad(srv,index,wot)
  %5 = extractvalue %dx.types.ResRet.f32 %4, 0
  %6 = extractvalue %dx.types.ResRet.f32 %4, 1
  %7 = extractvalue %dx.types.ResRet.f32 %4, 2
  %8 = add i32 %3, 1
  %9 = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %2, i32 %8, i32 0)  ; BufferLoad(srv,index,wot)
  %10 = extractvalue %dx.types.ResRet.f32 %9, 0
  %11 = extractvalue %dx.types.ResRet.f32 %9, 1
  %12 = extractvalue %dx.types.ResRet.f32 %9, 2
  %13 = fsub fast float %10, %5
  %14 = fsub fast float %11, %6
  %15 = fsub fast float %12, %7
  %16 = call float @dx.op.dot3.f32(i32 55, float %13, float %14, float %15, float %13, float %14, float %15)  ; Dot3(ax,ay,az,bx,by,bz)
  %17 = call float @dx.op.unary.f32(i32 25, float %16)  ; Rsqrt(value)
  %18 = fmul fast float %13, %17
  %19 = fmul fast float %14, %17
  %20 = fmul fast float %15, %17
  %21 = call float @dx.op.dot3.f32(i32 55, float %5, float %6, float %7, float %18, float %19, float %20)  ; Dot3(ax,ay,az,bx,by,bz)
  %22 = call float @dx.op.dot3.f32(i32 55, float %5, float %6, float %7, float %5, float %6, float %7)  ; Dot3(ax,ay,az,bx,by,bz)
  %23 = fmul fast float %21, %21
  %24 = fsub fast float 1.000000e+00, %22
  %25 = fadd fast float %24, %23
  %26 = fcmp fast ogt float %25, 0.000000e+00
  br i1 %26, label %27, label %31

; <label>:27                                      ; preds = %0
  %28 = fsub fast float -0.000000e+00, %21
  %29 = call float @dx.op.unary.f32(i32 24, float %25)  ; Sqrt(value)
  %30 = fsub fast float %28, %29
  br label %31

; <label>:31                                      ; preds = %27, %0
  %32 = phi float [ %30, %27 ], [ -1.000000e+00, %0 ]
  %33 = fmul fast float %32, %18
  %34 = fmul fast float %32, %19
  %35 = fmul fast float %32, %20
  %36 = fadd fast float %33, %5
  %37 = fadd fast float %34, %6
  %38 = fadd fast float %35, %7
  %39 = call float @dx.op.dot3.f32(i32 55, float %36, float %37, float %38, float %36, float %37, float %38)  ; Dot3(ax,ay,az,bx,by,bz)
  %40 = call float @dx.op.unary.f32(i32 25, float %39)  ; Rsqrt(value)
  %41 = fmul fast float %36, %40
  %42 = fmul fast float %37, %40
  %43 = fmul fast float %38, %40
  %44 = fcmp fast ogt float %32, 0.000000e+00
  %45 = select i1 %44, float %41, float 0.000000e+00
  %46 = select i1 %44, float %42, float 1.000000e+00
  %47 = select i1 %44, float %43, float 0.000000e+00
  %48 = call float @dx.op.dot3.f32(i32 55, float %18, float %19, float %20, float %45, float %46, float %47)  ; Dot3(ax,ay,az,bx,by,bz)
  %49 = fmul fast float %48, 2.000000e+00
  %50 = fmul fast float %49, %45
  %51 = fmul fast float %49, %46
  %52 = fmul fast float %49, %47
  %53 = fsub fast float %18, %50
  %54 = fsub fast float %19, %51
  %55 = fsub fast float %20, %52
  %56 = fsub fast float -0.000000e+00, %18
  %57 = fsub fast float -0.000000e+00, %19
  %58 = fsub fast float -0.000000e+00, %20
  %59 = call float @dx.op.dot3.f32(i32 55, float %56, float %57, float %58, float %45, float %46, float %47)  ; Dot3(ax,ay,az,bx,by,bz)
  %60 = call float @dx.op.unary.f32(i32 7, float %59)  ; Saturate(value)
  %61 = fsub fast float 1.000000e+00, %60
  %62 = call float @dx.op.unary.f32(i32 23, float %61)  ; Log(value)
  %63 = fmul fast float %62, 5.000000e+00
  %64 = call float @dx.op.unary.f32(i32 21, float %63)  ; Exp(value)
  %65 = fmul fast float %64, 0x3FEEB851E0000000
  %66 = fadd fast float %32, 0x3FA47AE160000000
  %67 = fadd fast float %66, %65
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %1, i32 %3, i32 0, float %53, float %54, float %55, float %67, i8 15)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

; Function Attrs: nounwind
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8) #2

; Function Attrs: nounwind readonly
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32) #1

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind readonly }
attributes #2 = { nounwind }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.entryPoints = !{!10}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"cs", i32 6, i32 0}
!4 = !{!5, !8, null, null}
!5 = !{!6}
!6 = !{i32 0, %"class.StructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 12, i32 0, !7}
!7 = !{i32 1, i32 16}
!8 = !{!9}
!9 = !{i32 0, %"class.RWStructuredBuffer<vector<float, 4> >"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !7}
!10 = !{void ()* @main, !"main", null, !4, !11}
!11 = !{i32 0, i64 16, i32 4, !12}
!12 = !{i32 64, i32 1, i32 1}
