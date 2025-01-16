set(SOURCES
    arch.cc
    archdefs.h
    arch.h
    arch_pybindings.cc
    arch_pybindings.h
    chipdb.h
    himbaechel_api.cc
    himbaechel_api.h
    himbaechel_constids.h
    himbaechel_gfxids.h
    himbaechel_helpers.cc
    himbaechel_helpers.h
)

add_nextpnr_architecture(${family}
    CORE_SOURCES ${SOURCES}
    MAIN_SOURCE  main.cc
)

function(add_nextpnr_himbaechel_microarchitecture microtarget)
    cmake_parse_arguments(arg "" "" "CORE_SOURCES;TEST_SOURCES" ${ARGN})

    target_sources(nextpnr-himbaechel-core INTERFACE ${arg_CORE_SOURCES})

    add_library(nextpnr-himbaechel-${microtarget}-chipdb INTERFACE)

    target_link_libraries(nextpnr-himbaechel-core INTERFACE nextpnr-himbaechel-${microtarget}-chipdb)

    if (BUILD_TESTS)
        target_sources(nextpnr-himbaechel-test PRIVATE ${arg_TEST_SOURCES})
    endif()
endfunction()

set(HIMBAECHEL_UARCHES example gowin xilinx ng-ultra)

set(HIMBAECHEL_UARCH "" CACHE STRING "Microarchitectures for nextpnr-himbaechel build")
set_property(CACHE HIMBAECHEL_UARCH PROPERTY STRINGS ${HIMBAECHEL_UARCHES})

if (NOT HIMBAECHEL_UARCH)
    message(STATUS "Microarchitecture needs to be set, set desired one with -DHIMBAECHEL_UARCH=xxx")
    message(STATUS "Supported Himbächel microarchitectures are :")
    message(STATUS "  all")
    foreach (item ${HIMBAECHEL_UARCHES})
        message(STATUS "  ${item}")
    endforeach()
    message(FATAL_ERROR "Microarchitecture setting is mandatory")
endif()

if (HIMBAECHEL_UARCH STREQUAL "all")
    set(HIMBAECHEL_UARCH ${HIMBAECHEL_UARCHES})
endif()

foreach (uarch ${HIMBAECHEL_UARCH})
    if (NOT uarch IN_LIST HIMBAECHEL_UARCHES)
        message(FATAL_ERROR "Microarchitecture ${uarch} is not a supported Himbächel microarchitecture")
    endif()

    message(STATUS "Configuring Himbächel microarchitecture: ${uarch}")
    add_subdirectory(uarch/${uarch})
endforeach()
