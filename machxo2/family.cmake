add_subdirectory(${family})
message(STATUS "Using MachXO2/XO3 chipdb: ${MACHXO2_CHIPDB}")

set(chipdb_sources)
set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${family}/chipdb)
foreach(device ${MACHXO2_DEVICES})
    set(chipdb_bba ${MACHXO2_CHIPDB}/chipdb-${device}.bba)
    set(chipdb_bin ${family}/chipdb/chipdb-${device}.bin)
    set(chipdb_cc  ${family}/chipdb/chipdb-${device}.cc)
    if(BBASM_MODE STREQUAL "binary")
        add_custom_command(
            OUTPUT ${chipdb_bin}
            COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${chipdb_bba} ${chipdb_bin}
            DEPENDS bbasm chipdb-${family}-bbas ${chipdb_bba})
        list(APPEND chipdb_binaries ${chipdb_bin})
    elseif(BBASM_MODE STREQUAL "embed")
        add_custom_command(
            OUTPUT ${chipdb_cc} ${chipdb_bin}
            COMMAND bbasm ${BBASM_ENDIAN_FLAG} --e ${chipdb_bba} ${chipdb_cc} ${chipdb_bin}
            DEPENDS bbasm chipdb-${family}-bbas ${chipdb_bba})
        list(APPEND chipdb_sources ${chipdb_cc})
        list(APPEND chipdb_binaries ${chipdb_bin})
    elseif(BBASM_MODE STREQUAL "string")
        add_custom_command(
            OUTPUT ${chipdb_cc}
            COMMAND bbasm ${BBASM_ENDIAN_FLAG} --c ${chipdb_bba} ${chipdb_cc}
            DEPENDS bbasm chipdb-${family}-bbas ${chipdb_bba})
        list(APPEND chipdb_sources ${chipdb_cc})
    endif()
endforeach()
if(WIN32)
    set(chipdb_rc ${CMAKE_CURRENT_BINARY_DIR}/${family}/resource/chipdb.rc)
    list(APPEND chipdb_sources ${chipdb_rc})

    file(WRITE ${chipdb_rc})
    foreach(device ${MACHXO2_DEVICES})
        file(APPEND ${chipdb_rc}
             "${family}/chipdb-${device}.bin RCDATA \"${CMAKE_CURRENT_BINARY_DIR}/${family}/chipdb/chipdb-${device}.bin\"")
    endforeach()
endif()

add_custom_target(chipdb-${family}-bins DEPENDS ${chipdb_sources} ${chipdb_binaries})

add_library(chipdb-${family} OBJECT ${MACHXO2_CHIPDB} ${chipdb_sources})
add_dependencies(chipdb-${family} chipdb-${family}-bins)
target_compile_options(chipdb-${family} PRIVATE -g0 -O0 -w)
target_compile_definitions(chipdb-${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(chipdb-${family} PRIVATE ${family})

configure_file(${family}/machxo2_available.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/machxo2_available.h)

foreach(family_target ${family_targets})
    target_sources(${family_target} PRIVATE $<TARGET_OBJECTS:chipdb-${family}>)
    target_sources(${family_target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/machxo2_available.h)
endforeach()
