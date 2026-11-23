unset(CPM_DIRECTORY CACHE)

include("${CMAKE_CURRENT_LIST_DIR}/CPM_0.43.2.cmake")

if (NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "CPM.cmake failed to initialise from ${CMAKE_CURRENT_LIST_DIR}.")
endif ()
