; shader hash: f370b0e11a43104bf197e82a8c252ed1
;
; Buffer Definitions:
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%struct.Payload = type { <3 x float>, float }
%struct.BuiltInTriangleIntersectionAttributes = type { <2 x float> }

; Function Attrs: nounwind
define void @"\01?AnyHit@@YAXUPayload@@UBuiltInTriangleIntersectionAttributes@@@Z"(%struct.Payload* noalias nocapture %payload, %struct.BuiltInTriangleIntersectionAttributes* nocapture readonly %attributes) #0 {
  %1 = getelementptr inbounds %struct.Payload, %struct.Payload* %payload, i32 0, i32 0
  %2 = load <3 x float>, <3 x float>* %1, align 4
  %3 = call float @dx.op.rayTCurrent.f32(i32 154)  ; RayTCurrent()
  %4 = getelementptr inbounds %struct.BuiltInTriangleIntersectionAttributes, %struct.BuiltInTriangleIntersectionAttributes* %attributes, i32 0, i32 0
  %5 = load <2 x float>, <2 x float>* %4, align 4
  %6 = extractelement <2 x float> %5, i32 0
  %7 = fcmp fast olt float %6, 0x3F847AE140000000
  %8 = getelementptr inbounds %struct.Payload, %struct.Payload* %payload, i32 0, i32 1
  br i1 %7, label %9, label %10

; <label>:9                                       ; preds = %0
  store <3 x float> %2, <3 x float>* %1, align 4
  store float %3, float* %8, align 4
  call void @dx.op.ignoreHit(i32 155)  ; IgnoreHit()
  unreachable

; <label>:10                                      ; preds = %0
  %11 = extractelement <2 x float> %5, i32 1
  %12 = fcmp fast ogt float %11, 0x3FEFAE1480000000
  br i1 %12, label %13, label %14

; <label>:13                                      ; preds = %10
  store <3 x float> %2, <3 x float>* %1, align 4
  store float %3, float* %8, align 4
  call void @dx.op.acceptHitAndEndSearch(i32 156)  ; AcceptHitAndEndSearch()
  unreachable

; <label>:14                                      ; preds = %10
  store <3 x float> %2, <3 x float>* %1, align 4
  store float %3, float* %8, align 4
  ret void
}

; Function Attrs: nounwind readonly
declare float @dx.op.rayTCurrent.f32(i32) #1

; Function Attrs: noreturn nounwind
declare void @dx.op.acceptHitAndEndSearch(i32) #2

; Function Attrs: noreturn nounwind
declare void @dx.op.ignoreHit(i32) #2

attributes #0 = { nounwind }
attributes #1 = { nounwind readonly }
attributes #2 = { noreturn nounwind }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.typeAnnotations = !{!4}
!dx.dxrPayloadAnnotations = !{!10}
!dx.entryPoints = !{!13, !14}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 9}
!2 = !{i32 1, i32 10}
!3 = !{!"lib", i32 6, i32 9}
!4 = !{i32 1, void (%struct.Payload*, %struct.BuiltInTriangleIntersectionAttributes*)* @"\01?AnyHit@@YAXUPayload@@UBuiltInTriangleIntersectionAttributes@@@Z", !5}
!5 = !{!6, !8, !9}
!6 = !{i32 1, !7, !7}
!7 = !{}
!8 = !{i32 2, !7, !7}
!9 = !{i32 0, !7, !7}
!10 = !{i32 0, %struct.Payload undef, !11}
!11 = !{!12, !12}
!12 = !{i32 0, i32 13107}
!13 = !{null, !"", null, null, null}
!14 = !{void (%struct.Payload*, %struct.BuiltInTriangleIntersectionAttributes*)* @"\01?AnyHit@@YAXUPayload@@UBuiltInTriangleIntersectionAttributes@@@Z", !"\01?AnyHit@@YAXUPayload@@UBuiltInTriangleIntersectionAttributes@@@Z", null, null, !15}
!15 = !{i32 8, i32 9, i32 6, i32 16, i32 7, i32 8, i32 5, !16}
!16 = !{i32 0}
