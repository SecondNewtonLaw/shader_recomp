; shader hash: f3db7a7ec456977583148168bb8fe4f6
;
; Buffer Definitions:
;
; cbuffer Camera
; {
;
;   struct hostlayout.Camera
;   {
;
;       column_major float4x4 inverseViewProjection;  ; Offset:    0
;       float3 cameraPosition;                        ; Offset:   64
;   
;   } Camera;                                         ; Offset:    0 Size:    76
;
; }
;
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Camera                            cbuffer      NA          NA     CB0            cb0     1
; Scene                             texture     i32         ras      T0             t0     1
; Output                                UAV     f32          2d      U0             u0     1
;
target datalayout = "e-m:e-p:32:32-i1:32-i8:32-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { i8* }
%hostlayout.Camera = type { [4 x <4 x float>], <3 x float> }
%struct.Payload = type { <3 x float>, float }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.CBufRet.f32 = type { float, float, float, float }
%struct.RaytracingAccelerationStructure = type { i32 }
%"class.RWTexture2D<vector<float, 4> >" = type { <4 x float> }

@"\01?Scene@@3URaytracingAccelerationStructure@@A" = external constant %dx.types.Handle, align 4
@"\01?Output@@3V?$RWTexture2D@V?$vector@M$03@@@@A" = external constant %dx.types.Handle, align 4
@Camera_legacy = external global %hostlayout.Camera

; Function Attrs: nounwind
define void @"\01?RayGen@@YAXXZ"() #0 {
  %1 = load %dx.types.Handle, %dx.types.Handle* @"\01?Scene@@3URaytracingAccelerationStructure@@A", align 4
  %2 = load %dx.types.Handle, %dx.types.Handle* @"\01?Output@@3V?$RWTexture2D@V?$vector@M$03@@@@A", align 4
  %3 = load %hostlayout.Camera, %hostlayout.Camera* @Camera_legacy
  %4 = alloca %struct.Payload, align 8
  %5 = call %dx.types.Handle @dx.op.createHandleForLib.hostlayout.Camera(i32 160, %hostlayout.Camera %3)  ; CreateHandleForLib(Resource)
  %6 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %5, %dx.types.ResourceProperties { i32 13, i32 76 })  ; AnnotateHandle(res,props)  resource: CBuffer
  %7 = call i32 @dx.op.dispatchRaysIndex.i32(i32 145, i8 0)  ; DispatchRaysIndex(col)
  %8 = insertelement <2 x i32> undef, i32 %7, i32 0
  %9 = call i32 @dx.op.dispatchRaysIndex.i32(i32 145, i8 1)  ; DispatchRaysIndex(col)
  %10 = insertelement <2 x i32> %8, i32 %9, i32 1
  %11 = call i32 @dx.op.dispatchRaysDimensions.i32(i32 146, i8 0)  ; DispatchRaysDimensions(col)
  %12 = insertelement <2 x i32> undef, i32 %11, i32 0
  %13 = call i32 @dx.op.dispatchRaysDimensions.i32(i32 146, i8 1)  ; DispatchRaysDimensions(col)
  %14 = insertelement <2 x i32> %12, i32 %13, i32 1
  %15 = uitofp <2 x i32> %10 to <2 x float>
  %16 = fadd fast <2 x float> %15, <float 5.000000e-01, float 5.000000e-01>
  %17 = uitofp <2 x i32> %14 to <2 x float>
  %18 = fdiv fast <2 x float> %16, %17
  %19 = fmul fast <2 x float> %18, <float 2.000000e+00, float 2.000000e+00>
  %20 = fadd fast <2 x float> %19, <float -1.000000e+00, float -1.000000e+00>
  %21 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 0)  ; CBufferLoadLegacy(handle,regIndex)
  %22 = extractvalue %dx.types.CBufRet.f32 %21, 0
  %23 = extractvalue %dx.types.CBufRet.f32 %21, 1
  %24 = extractvalue %dx.types.CBufRet.f32 %21, 3
  %25 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 1)  ; CBufferLoadLegacy(handle,regIndex)
  %26 = extractvalue %dx.types.CBufRet.f32 %25, 0
  %27 = extractvalue %dx.types.CBufRet.f32 %25, 1
  %28 = extractvalue %dx.types.CBufRet.f32 %25, 3
  %29 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 2)  ; CBufferLoadLegacy(handle,regIndex)
  %30 = extractvalue %dx.types.CBufRet.f32 %29, 0
  %31 = extractvalue %dx.types.CBufRet.f32 %29, 1
  %32 = extractvalue %dx.types.CBufRet.f32 %29, 3
  %33 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 3)  ; CBufferLoadLegacy(handle,regIndex)
  %34 = extractvalue %dx.types.CBufRet.f32 %33, 0
  %35 = extractvalue %dx.types.CBufRet.f32 %33, 1
  %36 = extractvalue %dx.types.CBufRet.f32 %33, 3
  %37 = extractelement <2 x float> %20, i64 0
  %38 = extractelement <2 x float> %20, i64 1
  %39 = fmul fast float %37, %22
  %40 = call float @dx.op.tertiary.f32(i32 46, float %38, float %23, float %39)  ; FMad(a,b,c)
  %41 = fadd fast float %40, %24
  %42 = insertelement <3 x float> undef, float %41, i32 0
  %43 = fmul fast float %37, %26
  %44 = call float @dx.op.tertiary.f32(i32 46, float %38, float %27, float %43)  ; FMad(a,b,c)
  %45 = fadd fast float %44, %28
  %46 = insertelement <3 x float> %42, float %45, i32 1
  %47 = fmul fast float %30, %37
  %48 = call float @dx.op.tertiary.f32(i32 46, float %38, float %31, float %47)  ; FMad(a,b,c)
  %49 = fadd fast float %48, %32
  %50 = insertelement <3 x float> %46, float %49, i32 2
  %51 = fmul fast float %34, %37
  %52 = call float @dx.op.tertiary.f32(i32 46, float %38, float %35, float %51)  ; FMad(a,b,c)
  %53 = fadd fast float %52, %36
  %54 = insertelement <3 x float> undef, float %53, i32 0
  %55 = shufflevector <3 x float> %54, <3 x float> undef, <3 x i32> zeroinitializer
  %56 = fdiv fast <3 x float> %50, %55
  %57 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %6, i32 4)  ; CBufferLoadLegacy(handle,regIndex)
  %58 = extractvalue %dx.types.CBufRet.f32 %57, 0
  %59 = extractvalue %dx.types.CBufRet.f32 %57, 1
  %60 = extractvalue %dx.types.CBufRet.f32 %57, 2
  %61 = insertelement <3 x float> undef, float %58, i64 0
  %62 = insertelement <3 x float> %61, float %59, i64 1
  %63 = insertelement <3 x float> %62, float %60, i64 2
  %64 = fsub fast <3 x float> %56, %63
  %65 = extractelement <3 x float> %64, i64 0
  %66 = extractelement <3 x float> %64, i64 1
  %67 = extractelement <3 x float> %64, i64 2
  %68 = call float @dx.op.dot3.f32(i32 55, float %65, float %66, float %67, float %65, float %66, float %67)  ; Dot3(ax,ay,az,bx,by,bz)
  %69 = call float @dx.op.unary.f32(i32 25, float %68)  ; Rsqrt(value)
  %70 = insertelement <3 x float> undef, float %69, i64 0
  %71 = insertelement <3 x float> %70, float %69, i64 1
  %72 = insertelement <3 x float> %71, float %69, i64 2
  %73 = fmul fast <3 x float> %64, %72
  %74 = call %dx.types.Handle @dx.op.createHandleForLib.dx.types.Handle(i32 160, %dx.types.Handle %1)  ; CreateHandleForLib(Resource)
  %75 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %74, %dx.types.ResourceProperties { i32 16, i32 0 })  ; AnnotateHandle(res,props)  resource: RTAccelerationStructure
  %76 = getelementptr inbounds %struct.Payload, %struct.Payload* %4, i32 0, i32 0
  store <3 x float> zeroinitializer, <3 x float>* %76, align 8
  %77 = getelementptr inbounds %struct.Payload, %struct.Payload* %4, i32 0, i32 1
  store float 1.000000e+09, float* %77, align 4
  %78 = extractelement <3 x float> %73, i64 0
  %79 = extractelement <3 x float> %73, i64 1
  %80 = extractelement <3 x float> %73, i64 2
  call void @dx.op.traceRay.struct.Payload(i32 157, %dx.types.Handle %75, i32 0, i32 255, i32 0, i32 1, i32 0, float %58, float %59, float %60, float 0x3F50624DE0000000, float %78, float %79, float %80, float 1.000000e+04, %struct.Payload* nonnull %4)  ; TraceRay(AccelerationStructure,RayFlags,InstanceInclusionMask,RayContributionToHitGroupIndex,MultiplierForGeometryContributionToShaderIndex,MissShaderIndex,Origin_X,Origin_Y,Origin_Z,TMin,Direction_X,Direction_Y,Direction_Z,TMax,payload)
  %81 = load <3 x float>, <3 x float>* %76, align 8
  %82 = load float, float* %77, align 4
  %83 = extractelement <3 x float> %81, i64 0
  %84 = extractelement <3 x float> %81, i64 1
  %85 = extractelement <3 x float> %81, i64 2
  %86 = fmul fast float %82, 0x3F847AE140000000
  %87 = call float @dx.op.unary.f32(i32 7, float %86)  ; Saturate(value)
  %88 = call %dx.types.Handle @dx.op.createHandleForLib.dx.types.Handle(i32 160, %dx.types.Handle %2)  ; CreateHandleForLib(Resource)
  %89 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %88, %dx.types.ResourceProperties { i32 4098, i32 1033 })  ; AnnotateHandle(res,props)  resource: RWTexture2D<4xF32>
  call void @dx.op.textureStore.f32(i32 67, %dx.types.Handle %89, i32 %7, i32 %9, i32 undef, float %83, float %84, float %85, float %87, i8 15)  ; TextureStore(srv,coord0,coord1,coord2,value0,value1,value2,value3,mask)
  ret void
}

; Function Attrs: nounwind readnone
declare i32 @dx.op.dispatchRaysDimensions.i32(i32, i8) #1

; Function Attrs: nounwind readnone
declare i32 @dx.op.dispatchRaysIndex.i32(i32, i8) #1

; Function Attrs: nounwind readnone
declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #1

; Function Attrs: nounwind readnone
declare float @dx.op.unary.f32(i32, float) #1

; Function Attrs: nounwind
declare void @dx.op.textureStore.f32(i32, %dx.types.Handle, i32, i32, i32, float, float, float, float, i8) #0

; Function Attrs: nounwind readonly
declare %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %dx.types.Handle, i32) #2

; Function Attrs: nounwind
declare void @dx.op.traceRay.struct.Payload(i32, %dx.types.Handle, i32, i32, i32, i32, i32, float, float, float, float, float, float, float, float, %struct.Payload*) #0

; Function Attrs: nounwind readnone
declare float @dx.op.tertiary.f32(i32, float, float, float) #1

; Function Attrs: nounwind readnone
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties) #1

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandleForLib.dx.types.Handle(i32, %dx.types.Handle) #2

; Function Attrs: nounwind readonly
declare %dx.types.Handle @dx.op.createHandleForLib.hostlayout.Camera(i32, %hostlayout.Camera) #2

attributes #0 = { nounwind }
attributes #1 = { nounwind readnone }
attributes #2 = { nounwind readonly }

!llvm.ident = !{!0}
!dx.version = !{!1}
!dx.valver = !{!2}
!dx.shaderModel = !{!3}
!dx.resources = !{!4}
!dx.typeAnnotations = !{!13}
!dx.dxrPayloadAnnotations = !{!17}
!dx.entryPoints = !{!20, !22}

!0 = !{!"dxcoob 1.10.2605.37 (c4d8f4f99)"}
!1 = !{i32 1, i32 9}
!2 = !{i32 1, i32 10}
!3 = !{!"lib", i32 6, i32 9}
!4 = !{!5, !8, !11, null}
!5 = !{!6}
!6 = !{i32 0, %struct.RaytracingAccelerationStructure* bitcast (%dx.types.Handle* @"\01?Scene@@3URaytracingAccelerationStructure@@A" to %struct.RaytracingAccelerationStructure*), !"Scene", i32 0, i32 0, i32 1, i32 16, i32 0, !7}
!7 = !{i32 0, i32 4}
!8 = !{!9}
!9 = !{i32 0, %"class.RWTexture2D<vector<float, 4> >"* bitcast (%dx.types.Handle* @"\01?Output@@3V?$RWTexture2D@V?$vector@M$03@@@@A" to %"class.RWTexture2D<vector<float, 4> >"*), !"Output", i32 0, i32 0, i32 1, i32 2, i1 false, i1 false, i1 false, !10}
!10 = !{i32 0, i32 9}
!11 = !{!12}
!12 = !{i32 0, %hostlayout.Camera* @Camera_legacy, !"Camera", i32 0, i32 0, i32 1, i32 76, null}
!13 = !{i32 1, void ()* @"\01?RayGen@@YAXXZ", !14}
!14 = !{!15}
!15 = !{i32 1, !16, !16}
!16 = !{}
!17 = !{i32 0, %struct.Payload undef, !18}
!18 = !{!19, !19}
!19 = !{i32 0, i32 13107}
!20 = !{null, !"", null, !4, !21}
!21 = !{i32 0, i64 8589934592}
!22 = !{void ()* @"\01?RayGen@@YAXXZ", !"\01?RayGen@@YAXXZ", null, null, !23}
!23 = !{i32 8, i32 7, i32 5, !24}
!24 = !{i32 0}
