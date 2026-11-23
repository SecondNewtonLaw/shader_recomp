; shader hash: 30cb63eb382baaf58b5cf34239aba121
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

%struct.IntersectionAttributes = type { <2 x float> }

; Function Attrs: nounwind
define void @"\01?Intersection@@YAXXZ"() #0 {
  %1 = alloca %struct.IntersectionAttributes, align 8
  %2 = call float @dx.op.objectRayOrigin.f32(i32 149, i8 0)  ; ObjectRayOrigin(col)
  %3 = insertelement <3 x float> undef, float %2, i64 0
  %4 = call float @dx.op.objectRayOrigin.f32(i32 149, i8 1)  ; ObjectRayOrigin(col)
  %5 = insertelement <3 x float> %3, float %4, i64 1
  %6 = call float @dx.op.objectRayOrigin.f32(i32 149, i8 2)  ; ObjectRayOrigin(col)
  %7 = insertelement <3 x float> %5, float %6, i64 2
  %8 = call float @dx.op.objectRayDirection.f32(i32 150, i8 0)  ; ObjectRayDirection(col)
  %9 = insertelement <3 x float> undef, float %8, i64 0
  %10 = call float @dx.op.objectRayDirection.f32(i32 150, i8 1)  ; ObjectRayDirection(col)
  %11 = insertelement <3 x float> %9, float %10, i64 1
  %12 = call float @dx.op.objectRayDirection.f32(i32 150, i8 2)  ; ObjectRayDirection(col)
  %13 = insertelement <3 x float> %11, float %12, i64 2
  %14 = call float @dx.op.dot.v3f32(i32 311, <3 x float> %7, <3 x float> %13)  ; FDot(a,b)
  %15 = call float @dx.op.dot.v3f32(i32 311, <3 x float> %7, <3 x float> %7)  ; FDot(a,b)
  %16 = fmul fast float %14, %14
  %17 = fsub fast float 1.000000e+00, %15
  %18 = fadd fast float %17, %16
  %19 = fcmp fast olt float %18, 0.000000e+00
  br i1 %19, label %34, label %20

; <label>:20                                      ; preds = %0
  %21 = fsub fast float -0.000000e+00, %14
  %22 = call float @dx.op.unary.f32(i32 24, float %18)  ; Sqrt(value)
  %23 = fsub fast float %21, %22
  %24 = call float @dx.op.rayTMin.f32(i32 153)  ; RayTMin()
  %25 = fcmp fast olt float %23, %24
  br i1 %25, label %34, label %26

; <label>:26                                      ; preds = %20
  %27 = call float @dx.op.rayTCurrent.f32(i32 154)  ; RayTCurrent()
  %28 = fcmp fast ogt float %23, %27
  br i1 %28, label %34, label %29

; <label>:29                                      ; preds = %26
  %30 = insertelement <2 x float> undef, float %23, i64 0
  %31 = insertelement <2 x float> %30, float 0.000000e+00, i64 1
  %32 = getelementptr inbounds %struct.IntersectionAttributes, %struct.IntersectionAttributes* %1, i32 0, i32 0
  store <2 x float> %31, <2 x float>* %32, align 8
  %33 = call i1 @dx.op.reportHit.struct.IntersectionAttributes(i32 158, float %23, i32 0, %struct.IntersectionAttributes* nonnull %1)  ; ReportHit(THit,HitKind,Attributes)
  br label %34

; <label>:34                                      ; preds = %29, %26, %20, %0
  ret void
}

; Function Attrs: nounwind readnone
declare float @dx.op.objectRayDirection.f32(i32, i8) #1

; Function Attrs: nounwind readnone
declare float @dx.op.objectRayOrigin.f32(i32, i8) #1

; Function Attrs: nounwind readnone
declare float @dx.op.dot.v3f32(i32, <3 x float>, <3 x float>) #1

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #1

; Function Attrs: nounwind readonly
declare float @dx.op.rayTCurrent.f32(i32) #2

; Function Attrs: nounwind readnone
declare float @dx.op.rayTMin.f32(i32) #1

; Function Attrs: nounwind
declare i1 @dx.op.reportHit.struct.IntersectionAttributes(i32, float, i32, %struct.IntersectionAttributes*) #0

attributes #0 = { nounwind }
attributes #1 = { nounwind readnone }
attributes #2 = { nounwind readonly }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.typeAnnotations = !{!4}
!dx.entryPoints = !{!8, !9}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 9}
!2 = !{i32 1, i32 10}
!3 = !{!"lib", i32 6, i32 9}
!4 = !{i32 1, void ()* @"\01?Intersection@@YAXXZ", !5}
!5 = !{!6}
!6 = !{i32 1, !7, !7}
!7 = !{}
!8 = !{null, !"", null, null, null}
!9 = !{void ()* @"\01?Intersection@@YAXXZ", !"\01?Intersection@@YAXXZ", null, null, !10}
!10 = !{i32 8, i32 8, i32 5, !11}
!11 = !{i32 0}
