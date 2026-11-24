cmake_minimum_required(VERSION 4.0)

project(shader_recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(USE_CCACHE "Use ccache as the compiler launcher when available." ON)
option(USE_LTO "Enable link-time optimization." OFF)
option(ENABLE_ASAN "Enable AddressSanitizer." OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer." OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors." ON)
option(ALLOW_NON_STANDARD_MATH "Enable non-standard fast-math optimizations." OFF)
option(SR_GLSLANG_ORACLE "Build glslang as an independent SPIR-V producer for the test suite." OFF)
option(BUILD_FUZZING "Build the SR.Fuzzing harness." OFF)
option(SR_USE_LIBFUZZER "Build SR.Fuzzing with libFuzzer when available." ON)

if (WIN32 AND NOT ENABLE_ASAN AND NOT ENABLE_UBSAN)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

if (USE_CCACHE)
    include(cmake/ccache.cmake)
endif ()

include(cmake/identify_compiler.cmake)

if (CXX_COMPILER_KIND STREQUAL "clang-cl")
    message(FATAL_ERROR "Clang (with MSVC front-end) is not supported! Please use clang with a GNU front-end, or the Visual C/C++ compiler instead.")
endif ()

include(cmake/CPM.cmake)
include(cmake/libassert.cmake)
include(cmake/mimalloc.cmake)
include(cmake/spirv.cmake)
include(cmake/zlib.cmake)

include(CTest)
if (BUILD_TESTING)
    include(cmake/catch2.cmake)
endif ()

if (SR_GLSLANG_ORACLE)
    include(cmake/glslang.cmake)
endif ()

set(CXX_COMPILE_OPTIONS "")
set(CXX_LINK_OPTIONS "")

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    set(CXX_COMPILE_OPTIONS
            -Wall -Wextra -Wpedantic -pedantic
            -ffp-contract=off
            -fno-fast-math
            -fno-math-errno
            -ffunction-sections
            -fdata-sections
            -fmerge-all-constants
            -fstack-protector-all
    )

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        list(APPEND CXX_COMPILE_OPTIONS -march=x86-64-v2)
    endif ()

    if (CXX_COMPILER_KIND STREQUAL "clang")
        list(APPEND CXX_COMPILE_OPTIONS
                -Wmicrosoft
                -Wno-c2y-extensions
                -Wno-gnu-anonymous-struct
                -Wno-language-extension-token
                -Wno-microsoft-cpp-macro
                -Wno-nested-anon-types
        )
    endif ()

    list(APPEND CXX_COMPILE_OPTIONS -Wno-unused-parameter)

    if (WARNINGS_AS_ERRORS)
        list(APPEND CXX_COMPILE_OPTIONS -Werror)
    endif ()

    if (ALLOW_NON_STANDARD_MATH)
        list(APPEND CXX_COMPILE_OPTIONS -ffast-math)
    endif ()

    if (ENABLE_ASAN)
        list(APPEND CXX_COMPILE_OPTIONS -fsanitize=address)
        list(APPEND CXX_LINK_OPTIONS -fsanitize=address)
    endif ()

    if (ENABLE_UBSAN)
        list(APPEND CXX_COMPILE_OPTIONS -fsanitize=undefined)
        list(APPEND CXX_LINK_OPTIONS -fsanitize=undefined)
    endif ()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND CXX_COMPILE_OPTIONS -Og)
    else ()
        list(APPEND CXX_COMPILE_OPTIONS -O3)
    endif ()

    list(APPEND CXX_COMPILE_OPTIONS -g3)

    if (WIN32)
        list(APPEND CXX_COMPILE_OPTIONS -gcodeview-ghash -fstandalone-debug)
        list(APPEND CXX_LINK_OPTIONS "-Wl,/DEBUG" "-Wl,/OPT:REF" "-Wl,/OPT:ICF")
    endif ()

    if (USE_LTO)
        if (ENABLE_ASAN OR ENABLE_UBSAN)
            message(FATAL_ERROR "You must disable Link Time Optimization to use sanitisers.")
        endif ()

        list(APPEND CXX_COMPILE_OPTIONS -flto=thin)

        if (CXX_COMPILER_KIND STREQUAL "clang")
            list(APPEND CXX_COMPILE_OPTIONS -fwhole-program-vtables)
            list(APPEND CXX_LINK_OPTIONS -fwhole-program-vtables)
        endif ()

        list(APPEND CXX_LINK_OPTIONS -flto=thin)

        if (WIN32)
            list(APPEND CXX_LINK_OPTIONS "-Wl,/opt:lldltojobs=all")
        endif ()
    endif ()

elseif (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if (ENABLE_UBSAN)
        message(FATAL_ERROR "The Undefined Behaviour Sanitiser is not available on MSVC front-end compilers!")
    endif ()

    set(CXX_COMPILE_OPTIONS
            /MP
            /W4
            /permissive-
            /Zc:preprocessor
            /Zc:inline
            /utf-8
            /Ob3
            /Ot

            /wd4068 # unknown pragma
            /wd4324 # structure was padded due to alignment specifier
            /wd4668 # '...' is not defined as a preprocessor macro
            /wd4702 # unreachable code
            /wd4820 # padding added after a member
    )

    if (WARNINGS_AS_ERRORS)
        list(APPEND CXX_COMPILE_OPTIONS /WX)
    endif ()

    if (ENABLE_ASAN)
        list(APPEND CXX_COMPILE_OPTIONS /fsanitize=address)
    endif ()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND CXX_COMPILE_OPTIONS /Od)
    else ()
        list(APPEND CXX_COMPILE_OPTIONS /O2)
    endif ()

    list(APPEND CXX_COMPILE_OPTIONS /Z7)

    if (USE_LTO)
        list(APPEND CXX_COMPILE_OPTIONS /GL)
        list(APPEND CXX_LINK_OPTIONS /LTCG /GL)
    endif ()

    list(APPEND CXX_LINK_OPTIONS /DEBUG /OPT:REF /OPT:ICF)
endif ()

set(SR_LIBFUZZER OFF)
if (BUILD_FUZZING AND SR_USE_LIBFUZZER AND
        CXX_COMPILER_KIND STREQUAL "clang" AND
        CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    set(SR_LIBFUZZER ON)
    list(APPEND CXX_COMPILE_OPTIONS -fsanitize=fuzzer-no-link)
endif ()

function(sr_add_library target)
    add_library(${target} STATIC ${ARGN})
    target_compile_options(${target} PRIVATE ${CXX_COMPILE_OPTIONS})
    target_link_options(${target} PRIVATE ${CXX_LINK_OPTIONS})
    target_link_libraries(${target} PRIVATE mimalloc-static)

    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${target}/include")
        target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/${target}/include")
    endif ()
endfunction()

function(sr_add_executable target)
    add_executable(${target} ${ARGN})
    target_compile_options(${target} PRIVATE ${CXX_COMPILE_OPTIONS})
    target_link_options(${target} PRIVATE ${CXX_LINK_OPTIONS})
    target_link_libraries(${target} PRIVATE mimalloc-static)
endfunction()

sr_add_library(SR.Common
        SR.Common/src/ByteReader.cpp
        SR.Common/src/Diagnostic.cpp
        SR.Common/src/IL/Analysis.cpp
        SR.Common/src/IL/Attributes.cpp
        SR.Common/src/IL/Builder.cpp
        SR.Common/src/IL/Dump.cpp
        SR.Common/src/IL/Instruction.cpp
        SR.Common/src/IL/Interpreter.cpp
        SR.Common/src/IL/Intrinsics.cpp
        SR.Common/src/IL/Module.cpp
        SR.Common/src/IL/Passes.cpp
        SR.Common/src/IL/Types.cpp
        SR.Common/src/IL/Verifier.cpp
)
if (TARGET libassert::assert)
    target_link_libraries(SR.Common PUBLIC libassert::assert)
endif ()

sr_add_library(SR.Frontend
        SR.Frontend/src/Arch/Gfx/GfxIsa.cpp
        SR.Frontend/src/Arch/Gfx/GfxInterpreter.cpp
        SR.Frontend/src/Arch/Gfx/GfxLifter.cpp
        SR.Frontend/src/Container/DxbcContainer.cpp
        SR.Frontend/src/Container/ElfContainer.cpp
        SR.Frontend/src/Container/XboxShaderContainer.cpp
        SR.Frontend/src/Frontend.cpp
        SR.Frontend/src/MasterReader.cpp
        SR.Frontend/src/Readers/CubinReader.cpp
        SR.Frontend/src/Readers/DxbcReader.cpp
        SR.Frontend/src/Readers/DxilReader.cpp
        SR.Frontend/src/Readers/HsacoReader.cpp
        SR.Frontend/src/Readers/SpirvReader.cpp
        SR.Frontend/src/Readers/XboxShaderReader.cpp
)
target_link_libraries(SR.Frontend PUBLIC SR.Common)
if (TARGET zlibstatic)
    # XBCH streams are zlib-wrapped FSSM programs.
    target_link_libraries(SR.Frontend PRIVATE zlibstatic)
endif ()

sr_add_library(SR.Backend
        SR.Backend/src/SpirvEmitter.cpp
)
target_link_libraries(SR.Backend PUBLIC SR.Common)
foreach (spirv_library SPIRV-Headers SPIRV-Tools-static SPIRV-Tools-opt)
    if (TARGET ${spirv_library})
        target_link_libraries(SR.Backend PUBLIC ${spirv_library})
    endif ()
endforeach ()

sr_add_executable(SR.CLI SR.CLI/main.cpp)
target_link_libraries(SR.CLI PRIVATE SR.Frontend SR.Backend)
set_target_properties(SR.CLI PROPERTIES OUTPUT_NAME "shader-recomp")

find_package(Vulkan QUIET)
if (Vulkan_FOUND)
    sr_add_executable(SR.ISAProbe SR.Tools/IsaProbe.cpp)
    target_link_libraries(SR.ISAProbe PRIVATE Vulkan::Vulkan)
    sr_add_executable(SR.VkRun SR.Tools/VkRun.cpp)
    target_link_libraries(SR.VkRun PRIVATE Vulkan::Vulkan)
    sr_add_executable(SR.VkDraw SR.Tools/VkDraw.cpp)
    target_link_libraries(SR.VkDraw PRIVATE Vulkan::Vulkan)
else ()
    message(STATUS "Vulkan was not found; SR.ISAProbe and SR.VkRun are disabled")
endif ()

if (BUILD_FUZZING)
    if (SR_LIBFUZZER)
        sr_add_executable(SR.Fuzzing SR.Fuzzing/fuzz_reader.cpp)
        target_link_libraries(SR.Fuzzing PRIVATE SR.Frontend)
        target_compile_options(SR.Fuzzing PRIVATE -fsanitize=fuzzer)
        target_link_options(SR.Fuzzing PRIVATE -fsanitize=fuzzer)
    else ()
        sr_add_executable(SR.Fuzzing SR.Fuzzing/smoke_reader.cpp)
        target_link_libraries(SR.Fuzzing PRIVATE SR.Frontend)
        target_compile_definitions(SR.Fuzzing PRIVATE
                SR_FUZZ_SEED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus")
    endif ()
endif ()

if (BUILD_TESTING)
    sr_add_executable(SR.Tests
            SR.Tests/CommonTests.cpp
            SR.Tests/BackendTests.cpp
            SR.Tests/FrontendTests.cpp
            SR.Tests/DxbcTests.cpp
            SR.Tests/CorpusReaderTests.cpp
            SR.Tests/FormatReaderTests.cpp
            SR.Tests/GfxTests.cpp
            SR.Tests/XboxTests.cpp
            SR.Tests/GraphicsTests.cpp
    )
    target_link_libraries(SR.Tests PRIVATE
            SR.Common
            SR.Frontend
            SR.Backend
            Catch2::Catch2WithMain
    )
    target_compile_definitions(SR.Tests PRIVATE
            SR_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/compiled"
            SR_CORPUS_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus"
    )
    catch_discover_tests(SR.Tests)
endif ()

if (WIN32 AND BUILD_TESTING)
    add_test(
            NAME corpus_validate
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/scripts/validate_corpus.ps1"
    )
endif ()
