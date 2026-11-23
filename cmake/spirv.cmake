CPMAddPackage(
        NAME SPIRV-Headers
        GITHUB_REPOSITORY KhronosGroup/SPIRV-Headers
        GIT_TAG vulkan-sdk-1.4.363.0
        OPTIONS
        "SPIRV_HEADERS_ENABLE_TESTS OFF"
        "SPIRV_HEADERS_ENABLE_INSTALL OFF"
)

CPMAddPackage(
        NAME SPIRV-Tools
        GITHUB_REPOSITORY KhronosGroup/SPIRV-Tools
        GIT_TAG vulkan-sdk-1.4.363.0
        OPTIONS
        "SPIRV_SKIP_EXECUTABLES ON"
        "SPIRV_SKIP_TESTS ON"
        "SPIRV_TOOLS_BUILD_STATIC ON"
        "SKIP_SPIRV_TOOLS_INSTALL ON"
        "SPIRV_WERROR OFF"
        "SPIRV_BUILD_FUZZER OFF"
        "SPIRV_BUILD_LIBFUZZER_TARGETS OFF"
        "SPIRV_BUILD_COMPRESSION OFF"
        "SPIRV_TOOLS_USE_MIMALLOC OFF"
        "BUILD_SHARED_LIBS OFF"
)

if (NOT TARGET SPIRV-Tools-static)
    message(FATAL_ERROR "SPIRV-Tools did not define SPIRV-Tools-static; check cmake/spirv.cmake options.")
endif ()

foreach (spirv_target
        SPIRV-Headers
        SPIRV-Tools-static
        SPIRV-Tools-opt
        SPIRV-Tools-link
        SPIRV-Tools-reduce)
    if (TARGET ${spirv_target})
        set_target_properties(${spirv_target} PROPERTIES SYSTEM ON)
    endif ()
endforeach ()
