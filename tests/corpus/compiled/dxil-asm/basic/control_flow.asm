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
; shader hash: 0eb7a4a0aad3501d308783c63cd0c0f8
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
;       uint count;                                   ; Offset:    0
;       uint limit;                                   ; Offset:    4
;   
;   } Params;                                         ; Offset:    0 Size:     8
;
; }
;
; Resource bind info for Output
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
; Output                                UAV  struct         r/w      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%dx.types.CBufRet.i32 = type { i32, i32, i32, i32 }
%"class.RWStructuredBuffer<unsigned int>" = type { i32 }
%Params = type { i32, i32 }

define void @main() {
  %1 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %2 = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 0, i1 false)  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)
  %3 = call i32 @dx.op.threadId.i32(i32 93, i32 0)  ; ThreadId(component)
  %4 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %2, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %5 = extractvalue %dx.types.CBufRet.i32 %4, 0
  %6 = icmp eq i32 %5, 0
  br i1 %6, label %35, label %7

; <label>:7                                       ; preds = %0
  br label %8

; <label>:8                                       ; preds = %28, %7
  %9 = phi i32 [ %29, %28 ], [ 0, %7 ]
  %10 = phi i32 [ %30, %28 ], [ 0, %7 ]
  %11 = and i32 %10, 3
  %12 = icmp eq i32 %11, 0
  br i1 %12, label %13, label %18

; <label>:13                                      ; preds = %8
  %14 = mul i32 %3, 1664525
  %15 = mul i32 %10, 1013904223
  %16 = add i32 %9, %14
  %17 = add i32 %16, %15
  br label %28

; <label>:18                                      ; preds = %8
  %19 = icmp eq i32 %11, 1
  br i1 %19, label %20, label %26

; <label>:20                                      ; preds = %18
  %21 = mul i32 %3, 1664525
  %22 = mul i32 %10, 1013904223
  %23 = add i32 %21, 1013904223
  %24 = add i32 %23, %22
  %25 = xor i32 %24, %9
  br label %28

; <label>:26                                      ; preds = %18
  %27 = sub i32 %9, %10
  br label %28

; <label>:28                                      ; preds = %26, %20, %13
  %29 = phi i32 [ %17, %13 ], [ %25, %20 ], [ %27, %26 ]
  %30 = add i32 %10, 1
  %31 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %2, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %32 = extractvalue %dx.types.CBufRet.i32 %31, 0
  %33 = icmp ult i32 %30, %32
  br i1 %33, label %8, label %34

; <label>:34                                      ; preds = %28
  br label %35

; <label>:35                                      ; preds = %34, %0
  %36 = phi i32 [ 0, %0 ], [ %29, %34 ]
  %37 = and i32 %3, 3
  switch i32 %37, label %46 [
    i32 0, label %38
    i32 1, label %42
    i32 2, label %44
  ]

; <label>:38                                      ; preds = %35
  %39 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %2, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %40 = extractvalue %dx.types.CBufRet.i32 %39, 1
  %41 = add i32 %40, %36
  br label %48

; <label>:42                                      ; preds = %35
  %43 = mul i32 %36, 3
  br label %48

; <label>:44                                      ; preds = %35
  %45 = xor i32 %36, -1
  br label %48

; <label>:46                                      ; preds = %35
  %47 = xor i32 %36, -1515870811
  br label %48

; <label>:48                                      ; preds = %46, %44, %42, %38
  %49 = phi i32 [ %47, %46 ], [ %45, %44 ], [ %43, %42 ], [ %41, %38 ]
  %50 = call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, %dx.types.Handle %2, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %51 = extractvalue %dx.types.CBufRet.i32 %50, 1
  %52 = icmp ugt i32 %49, %51
  br i1 %52, label %54, label %53

; <label>:53                                      ; preds = %48
  call void @dx.op.bufferStore.i32(i32 69, %dx.types.Handle %1, i32 %3, i32 0, i32 %49, i32 undef, i32 undef, i32 undef, i8 1)  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)
  br label %54

; <label>:54                                      ; preds = %53, %48
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.threadId.i32(i32, i32) #0

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %dx.types.Handle, i32) #1

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1) #1

; Function Attrs: nounwind
declare void @dx.op.bufferStore.i32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i8) #2

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
!4 = !{null, !5, !8, null}
!5 = !{!6}
!6 = !{i32 0, %"class.RWStructuredBuffer<unsigned int>"* undef, !"", i32 0, i32 0, i32 1, i32 12, i1 false, i1 false, i1 false, !7}
!7 = !{i32 1, i32 4}
!8 = !{!9}
!9 = !{i32 0, %Params* undef, !"", i32 0, i32 0, i32 1, i32 8, null}
!10 = !{void ()* @main, !"main", null, !4, !11}
!11 = !{i32 0, i64 16, i32 4, !12}
!12 = !{i32 64, i32 1, i32 1}
