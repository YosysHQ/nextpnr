include(FindOxide)

set(SOURCES
    arch.cc
    archdefs.h
    arch.h
    arch_place.cc
    arch_pybindings.cc
    arch_pybindings.h
    bba_version.inc
    constids.inc
    fasm.cc
    global.cc
    io.cc
    pack.cc
    pdc.cc
    pins.cc
    post_place.cc
)

add_nextpnr_architecture(${family}
    CORE_SOURCES ${SOURCES}
    MAIN_SOURCE  main.cc
)

# NOTE: Unlike iCE40 and ECP5; one database can cover all densities of a given family
set(ALL_NEXUS_FAMILIES LIFCL)
set(NEXUS_FAMILIES ${ALL_NEXUS_FAMILIES} CACHE STRING
    "Include support for these Nexus families (available: ${ALL_NEXUS_FAMILIES})")
message(STATUS "Enabled Nexus families: ${NEXUS_FAMILIES}")

foreach (subfamily ${NEXUS_FAMILIES})
    if (NOT subfamily IN_LIST ALL_NEXUS_FAMILIES)
        message(FATAL_ERROR "${subfamily} is not a supported Nexus family")
    endif()

    add_bba_produce_command(
        COMMAND ${PRJOXIDE_TOOL}
            bba-export ${subfamily}
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${subfamily}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${subfamily}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/bba_version.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
    )

    add_bba_compile_command(
        TARGET  nextpnr-${family}-chipdb
        OUTPUT  ${family}/chipdb-${subfamily}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${subfamily}.bba
        MODE    ${BBASM_MODE}
    )
endforeach()
