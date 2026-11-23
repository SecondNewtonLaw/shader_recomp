CPMAddPackage(
        NAME libassert
        GITHUB_REPOSITORY jeremy-rifkin/libassert
        GIT_TAG v2.2.0
        PATCH_COMMAND ${CMAKE_COMMAND}
        -D "SOURCE_DIR=<SOURCE_DIR>"
        -P "${CMAKE_CURRENT_LIST_DIR}/patches/libassert-add-algorithm-include.cmake"
        OPTIONS
        "LIBASSERT_USE_MAGIC_ENUM ON"
)
