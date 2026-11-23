; shader hash: 4405a6f5d59470ae51a4dc0f3033ec27
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

; Function Attrs: nounwind
define void @"\01?Miss@@YAXUPayload@@@Z"(%struct.Payload* noalias nocapture %payload) #0 {
  %1 = call float @dx.op.worldRayDirection.f32(i32 148, i8 0)  ; WorldRayDirection(col)
  %2 = insertelement <3 x float> undef, float %1, i64 0
  %3 = call float @dx.op.worldRayDirection.f32(i32 148, i8 1)  ; WorldRayDirection(col)
  %4 = insertelement <3 x float> %2, float %3, i64 1
  %5 = call float @dx.op.worldRayDirection.f32(i32 148, i8 2)  ; WorldRayDirection(col)
  %6 = insertelement <3 x float> %4, float %5, i64 2
  %7 = fadd fast <3 x float> %6, <float 0x3FA99999A0000000, float 0x3FB99999A0000000, float 0x3FC99999A0000000>
  %8 = getelementptr inbounds %struct.Payload, %struct.Payload* %payload, i32 0, i32 0
  store <3 x float> %7, <3 x float>* %8, align 4
  %9 = getelementptr inbounds %struct.Payload, %struct.Payload* %payload, i32 0, i32 1
  store float 1.000000e+09, float* %9, align 4
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.worldRayDirection.f32(i32, i8) #1

attributes #0 = { nounwind }
attributes #1 = { nounwind readnone }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.typeAnnotations = !{!4}
!dx.dxrPayloadAnnotations = !{!9}
!dx.entryPoints = !{!12, !13}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 9}
!2 = !{i32 1, i32 10}
!3 = !{!"lib", i32 6, i32 9}
!4 = !{i32 1, void (%struct.Payload*)* @"\01?Miss@@YAXUPayload@@@Z", !5}
!5 = !{!6, !8}
!6 = !{i32 1, !7, !7}
!7 = !{}
!8 = !{i32 2, !7, !7}
!9 = !{i32 0, %struct.Payload undef, !10}
!10 = !{!11, !11}
!11 = !{i32 0, i32 13107}
!12 = !{null, !"", null, null, null}
!13 = !{void (%struct.Payload*)* @"\01?Miss@@YAXUPayload@@@Z", !"\01?Miss@@YAXUPayload@@@Z", null, null, !14}
!14 = !{i32 8, i32 11, i32 6, i32 16, i32 5, !15}
!15 = !{i32 0}
