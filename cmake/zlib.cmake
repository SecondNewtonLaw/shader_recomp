CPMAddPackage(
        NAME zlib
        GITHUB_REPOSITORY madler/zlib
        GIT_TAG v1.3.1
        OPTIONS
        "ZLIB_BUILD_EXAMPLES OFF"
        "SKIP_INSTALL ON"
)
