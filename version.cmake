# If VERSION is already provided (CI), use it
if(DEFINED ENV{VERSION})
    set(PROJECT_VER "$ENV{VERSION}")
else()
    execute_process(
        COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(PROJECT_VER "${GIT_VERSION}")
endif()
