;
; Input signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; POSITION                 0   xyz         0     NONE   float   xyz 
; NORMAL                   0   xyz         1     NONE   float   xyz 
; TEXCOORD                 0   xy          2     NONE   float   xy  
;
;
; Output signature:
;
; Name                 Index   Mask Register SysValue  Format   Used
; -------------------- ----- ------ -------- -------- ------- ------
; SV_Position              0   xyzw        0      POS   float   xyzw
; TEXCOORD                 0   xyz         1     NONE   float   xyz 
; TEXCOORD                 1   xy          2     NONE   float   xy  
;
; shader hash: 59ad717d8bce213ec595f930867db429
;
; Pipeline Runtime Information: 
;
;PSVRuntimeInfo:
; Vertex Shader
; OutputPositionPresent=1
; MinimumExpectedWaveLaneCount: 0
; MaximumExpectedWaveLaneCount: 4294967295
; UsesViewID: false
; SigInputElements: 3
; SigOutputElements: 3
; SigPatchConstOrPrimElements: 0
; SigInputVectors: 3
; SigOutputVectors[0]: 3
; SigOutputVectors[1]: 0
; SigOutputVectors[2]: 0
; SigOutputVectors[3]: 0
; EntryFunctionName: main
;
;
; Input signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; POSITION                 0                              
; NORMAL                   0                              
; TEXCOORD                 0                              
;
; Output signature:
;
; Name                 Index             InterpMode DynIdx
; -------------------- ----- ---------------------- ------
; SV_Position              0          noperspective       
; TEXCOORD                 0                 linear       
; TEXCOORD                 1                 linear       
;
; Buffer Definitions:
;
; cbuffer Transforms
; {
;
;   struct hostlayout.Transforms
;   {
;
;       column_major float4x4 world;                  ; Offset:    0
;       column_major float4x4 viewProjection;         ; Offset:   64
;   
;   } Transforms;                                     ; Offset:    0 Size:   128
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Transforms                        cbuffer      NA          NA     CB0            cb0     1
;
;
; ViewId state:
;
; Number of inputs: 10, outputs: 10
; Outputs dependent on ViewId: {  }
; Inputs contributing to computation of Outputs:
;   output 0 depends on inputs: { 0, 1, 2 }
;   output 1 depends on inputs: { 0, 1, 2 }
;   output 2 depends on inputs: { 0, 1, 2 }
;   output 3 depends on inputs: { 0, 1, 2 }
;   output 4 depends on inputs: { 4, 5, 6 }
;   output 5 depends on inputs: { 4, 5, 6 }
;   output 6 depends on inputs: { 4, 5, 6 }
;   output 8 depends on inputs: { 8 }
;   output 9 depends on inputs: { 9 }
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%hostlayout.Transforms = type { [4 x <4 x float>], [4 x <4 x float>] }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call float @dx.op.loadInput.f32(i32 4, i32 2, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %3 = call float @dx.op.loadInput.f32(i32 4, i32 2, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %4 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %5 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %6 = call float @dx.op.loadInput.f32(i32 4, i32 1, i32 0, i8 2, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %7 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %8 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 1, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %9 = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 2, i32 undef)  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)
  %10 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %11 = extractvalue %dx.types.CBufRet.f32 %10, 0
  %12 = extractvalue %dx.types.CBufRet.f32 %10, 1
  %13 = extractvalue %dx.types.CBufRet.f32 %10, 2
  %14 = extractvalue %dx.types.CBufRet.f32 %10, 3
  %15 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %16 = extractvalue %dx.types.CBufRet.f32 %15, 0
  %17 = extractvalue %dx.types.CBufRet.f32 %15, 1
  %18 = extractvalue %dx.types.CBufRet.f32 %15, 2
  %19 = extractvalue %dx.types.CBufRet.f32 %15, 3
  %20 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %21 = extractvalue %dx.types.CBufRet.f32 %20, 0
  %22 = extractvalue %dx.types.CBufRet.f32 %20, 1
  %23 = extractvalue %dx.types.CBufRet.f32 %20, 2
  %24 = extractvalue %dx.types.CBufRet.f32 %20, 3
  %25 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %26 = extractvalue %dx.types.CBufRet.f32 %25, 0
  %27 = extractvalue %dx.types.CBufRet.f32 %25, 1
  %28 = extractvalue %dx.types.CBufRet.f32 %25, 2
  %29 = extractvalue %dx.types.CBufRet.f32 %25, 3
  %30 = fmul fast float %11, %7
  %31 = call float @dx.op.tertiary.f32(i32 46, float %8, float %12, float %30)  ; FMad(a,b,c)
  %32 = call float @dx.op.tertiary.f32(i32 46, float %9, float %13, float %31)  ; FMad(a,b,c)
  %33 = fadd fast float %32, %14
  %34 = fmul fast float %16, %7
  %35 = call float @dx.op.tertiary.f32(i32 46, float %8, float %17, float %34)  ; FMad(a,b,c)
  %36 = call float @dx.op.tertiary.f32(i32 46, float %9, float %18, float %35)  ; FMad(a,b,c)
  %37 = fadd fast float %36, %19
  %38 = fmul fast float %21, %7
  %39 = call float @dx.op.tertiary.f32(i32 46, float %8, float %22, float %38)  ; FMad(a,b,c)
  %40 = call float @dx.op.tertiary.f32(i32 46, float %9, float %23, float %39)  ; FMad(a,b,c)
  %41 = fadd fast float %40, %24
  %42 = fmul fast float %26, %7
  %43 = call float @dx.op.tertiary.f32(i32 46, float %8, float %27, float %42)  ; FMad(a,b,c)
  %44 = call float @dx.op.tertiary.f32(i32 46, float %9, float %28, float %43)  ; FMad(a,b,c)
  %45 = fadd fast float %44, %29
  %46 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %47 = extractvalue %dx.types.CBufRet.f32 %46, 0
  %48 = extractvalue %dx.types.CBufRet.f32 %46, 1
  %49 = extractvalue %dx.types.CBufRet.f32 %46, 2
  %50 = extractvalue %dx.types.CBufRet.f32 %46, 3
  %51 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 5)  ; CBufferLoadLegacy(handle,regIndex)
  %52 = extractvalue %dx.types.CBufRet.f32 %51, 0
  %53 = extractvalue %dx.types.CBufRet.f32 %51, 1
  %54 = extractvalue %dx.types.CBufRet.f32 %51, 2
  %55 = extractvalue %dx.types.CBufRet.f32 %51, 3
  %56 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 6)  ; CBufferLoadLegacy(handle,regIndex)
  %57 = extractvalue %dx.types.CBufRet.f32 %56, 0
  %58 = extractvalue %dx.types.CBufRet.f32 %56, 1
  %59 = extractvalue %dx.types.CBufRet.f32 %56, 2
  %60 = extractvalue %dx.types.CBufRet.f32 %56, 3
  %61 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %1, i32 7)  ; CBufferLoadLegacy(handle,regIndex)
  %62 = extractvalue %dx.types.CBufRet.f32 %61, 0
  %63 = extractvalue %dx.types.CBufRet.f32 %61, 1
  %64 = extractvalue %dx.types.CBufRet.f32 %61, 2
  %65 = extractvalue %dx.types.CBufRet.f32 %61, 3
  %66 = fmul fast float %47, %33
  %67 = call float @dx.op.tertiary.f32(i32 46, float %37, float %48, float %66)  ; FMad(a,b,c)
  %68 = call float @dx.op.tertiary.f32(i32 46, float %41, float %49, float %67)  ; FMad(a,b,c)
  %69 = call float @dx.op.tertiary.f32(i32 46, float %45, float %50, float %68)  ; FMad(a,b,c)
  %70 = fmul fast float %52, %33
  %71 = call float @dx.op.tertiary.f32(i32 46, float %37, float %53, float %70)  ; FMad(a,b,c)
  %72 = call float @dx.op.tertiary.f32(i32 46, float %41, float %54, float %71)  ; FMad(a,b,c)
  %73 = call float @dx.op.tertiary.f32(i32 46, float %45, float %55, float %72)  ; FMad(a,b,c)
  %74 = fmul fast float %57, %33
  %75 = call float @dx.op.tertiary.f32(i32 46, float %37, float %58, float %74)  ; FMad(a,b,c)
  %76 = call float @dx.op.tertiary.f32(i32 46, float %41, float %59, float %75)  ; FMad(a,b,c)
  %77 = call float @dx.op.tertiary.f32(i32 46, float %45, float %60, float %76)  ; FMad(a,b,c)
  %78 = fmul fast float %62, %33
  %79 = call float @dx.op.tertiary.f32(i32 46, float %37, float %63, float %78)  ; FMad(a,b,c)
  %80 = call float @dx.op.tertiary.f32(i32 46, float %41, float %64, float %79)  ; FMad(a,b,c)
  %81 = call float @dx.op.tertiary.f32(i32 46, float %45, float %65, float %80)  ; FMad(a,b,c)
  %82 = fmul fast float %11, %4
  %83 = call float @dx.op.tertiary.f32(i32 46, float %5, float %12, float %82)  ; FMad(a,b,c)
  %84 = call float @dx.op.tertiary.f32(i32 46, float %6, float %13, float %83)  ; FMad(a,b,c)
  %85 = fmul fast float %16, %4
  %86 = call float @dx.op.tertiary.f32(i32 46, float %5, float %17, float %85)  ; FMad(a,b,c)
  %87 = call float @dx.op.tertiary.f32(i32 46, float %6, float %18, float %86)  ; FMad(a,b,c)
  %88 = fmul fast float %21, %4
  %89 = call float @dx.op.tertiary.f32(i32 46, float %5, float %22, float %88)  ; FMad(a,b,c)
  %90 = call float @dx.op.tertiary.f32(i32 46, float %6, float %23, float %89)  ; FMad(a,b,c)
  %91 = call float @dx.op.dot3.f32(i32 55, float %84, float %87, float %90, float %84, float %87, float %90)  ; Dot3(ax,ay,az,bx,by,bz)
  %92 = call float @dx.op.unary.f32(i32 25, float %91)  ; Rsqrt(value)
  %93 = fmul fast float %92, %84
  %94 = fmul fast float %92, %87
  %95 = fmul fast float %92, %90
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %69)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 1, float %73)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 2, float %77)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 3, float %81)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 0, float %93)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 1, float %94)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 1, i32 0, i8 2, float %95)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 0, float %2)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  call void @dx.op.storeOutput.f32(i32 5, i32 2, i32 0, i8 1, float %3)  ; StoreOutput(outputSigId,rowIndex,colIndex,value)
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.loadInput.f32(i32, i32, i32, i8, i32) #0

; Function Attrs: nounwind
declare void @dx.op.storeOutput.f32(i32, i32, i32, i8, float) #1

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #0

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #2

attributes #0 = { nounwind readnone }
attributes #1 = { nounwind }
attributes #2 = { nounwind readonly }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.viewIdState = !{!7}
!dx.entryPoints = !{!8}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 0}
!2 = !{i32 1, i32 10}
!3 = !{!"vs", i32 6, i32 0}
!4 = !{null, null, !5, null}
!5 = !{!6}
!6 = !{i32 0, %hostlayout.Transforms* undef, !"", i32 0, i32 0, i32 1, i32 128, null}
!7 = !{[12 x i32] [i32 10, i32 10, i32 15, i32 15, i32 15, i32 0, i32 112, i32 112, i32 112, i32 0, i32 256, i32 512]}
!8 = !{void ()* @main, !"main", !9, !4, null}
!9 = !{!10, !17, null}
!10 = !{!11, !14, !15}
!11 = !{i32 0, !"POSITION", i8 9, i8 0, !12, i8 0, i32 1, i8 3, i32 0, i8 0, !13}
!12 = !{i32 0}
!13 = !{i32 3, i32 7}
!14 = !{i32 1, !"NORMAL", i8 9, i8 0, !12, i8 0, i32 1, i8 3, i32 1, i8 0, !13}
!15 = !{i32 2, !"TEXCOORD", i8 9, i8 0, !12, i8 0, i32 1, i8 2, i32 2, i8 0, !16}
!16 = !{i32 3, i32 3}
!17 = !{!18, !20, !21}
!18 = !{i32 0, !"SV_Position", i8 9, i8 3, !12, i8 4, i32 1, i8 4, i32 0, i8 0, !19}
!19 = !{i32 3, i32 15}
!20 = !{i32 1, !"TEXCOORD", i8 9, i8 0, !12, i8 2, i32 1, i8 3, i32 1, i8 0, !13}
!21 = !{i32 2, !"TEXCOORD", i8 9, i8 0, !22, i8 2, i32 1, i8 2, i32 2, i8 0, !16}
!22 = !{i32 1}
