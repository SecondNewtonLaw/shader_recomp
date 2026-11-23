CPMAddPackage(
        NAME mimalloc
        GITHUB_REPOSITORY microsoft/mimalloc
        GIT_TAG f8401befa675adb13b98decababd7fdc59572477
        OPTIONS
        "MI_BUILD_SHARED OFF"
        "MI_BUILD_STATIC ON"
        "MI_BUILD_OBJECT OFF"
        "MI_BUILD_TESTS OFF"
        "MI_OVERRIDE OFF"
        "MI_STATS OFF"
)

if (TARGET mimalloc-static)
    set_target_properties(mimalloc-static PROPERTIES SYSTEM ON)
endif ()
