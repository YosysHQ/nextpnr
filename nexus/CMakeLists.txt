include(FindOxide)

# NOTE: Unlike iCE40 and ECP5; one database can cover all densities of a given family
set(ALL_NEXUS_FAMILIES LIFCL)
set(NEXUS_FAMILIES ${ALL_NEXUS_FAMILIES} CACHE STRING
    "Include support for these Nexus families (available: ${ALL_NEXUS_FAMILIES})")
message(STATUS "Enabled Nexus families: ${NEXUS_FAMILIES}")

add_library(chipdb-${family} OBJECT)
target_compile_definitions(chipdb-${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(chipdb-${family} PRIVATE .)

foreach (family_target ${family_targets})
    target_link_libraries(${family_target} PRIVATE chipdb-${family})
endforeach()

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
        TARGET  chipdb-${family}
        OUTPUT  ${CMAKE_BINARY_DIR}/share/${family}/chipdb-${subfamily}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${subfamily}.bba
        IDENT   ${family}/chipdb-${subfamily}.bin
        MODE    ${BBASM_MODE}
    )
endforeach()
