add_subdirectory(${family})
message(STATUS "Using iCE40 chipdb: ${ICE40_CHIPDB}")

add_library(chipdb-${family} OBJECT)
target_compile_options(chipdb-${family} PRIVATE -w -g0 -O0)
target_compile_definitions(chipdb-${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(chipdb-${family} PRIVATE ${family})

foreach (family_target ${family_targets})
    target_link_libraries(${family_target} PRIVATE chipdb-${family})
endforeach()

foreach (device ${ICE40_DEVICES})
    add_bba_compile_command(
        DEPENDS chipdb-${family}-bbas
        TARGET  chipdb-${family}
        OUTPUT  ${family}/chipdb-${device}.bin
        INPUT   ${ICE40_CHIPDB}/chipdb-${device}.bba
        MODE    ${BBASM_MODE}
    )
endforeach()
