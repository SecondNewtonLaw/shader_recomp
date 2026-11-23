CPMAddPackage(
        NAME glslang
        GITHUB_REPOSITORY KhronosGroup/glslang
        GIT_TAG vulkan-sdk-1.4.363.0
        OPTIONS
        "BUILD_EXTERNAL OFF"
        "BUILD_SHARED_LIBS OFF"
        "ENABLE_OPT OFF"
        "ENABLE_HLSL ON"
        "ENABLE_GLSLANG_BINARIES OFF"
        "ENABLE_PCH OFF"
        "GLSLANG_TESTS OFF"
        "GLSLANG_ENABLE_INSTALL OFF"
)

foreach (glslang_target
        glslang
        glslang-default-resource-limits
        SPIRV
        MachineIndependent
        GenericCodeGen
        OSDependent)
    if (TARGET ${glslang_target})
        set_target_properties(${glslang_target} PROPERTIES SYSTEM ON)
    endif ()
endforeach ()
