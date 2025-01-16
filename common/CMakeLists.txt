if (NOT DEFINED CURRENT_GIT_VERSION)
    # Get the latest abbreviated commit hash of the working branch if not already defined outside
    # (e.g. by package manager when building outside of git repository).
    execute_process(
        COMMAND git describe --tags --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE CURRENT_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h)

add_library(nextpnr_version INTERFACE)

target_include_directories(nextpnr_version INTERFACE ${CMAKE_CURRENT_BINARY_DIR})

target_sources(nextpnr_version INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
)
