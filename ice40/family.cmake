add_subdirectory(${family})
message(STATUS "Using iCE40 chipdb: ${ICE40_CHIPDB}")

set(chipdb_sources)
set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${family}/chipdb)
foreach(device ${ICE40_DEVICES})
    set(chipdb_bba ${ICE40_CHIPDB}/chipdb-${device}.bba)
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
    list(APPEND chipdb_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/${family}/resource/embed.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/${family}/resource/chipdb.rc)
endif()

add_custom_target(chipdb-${family}-bins DEPENDS ${chipdb_sources} ${chipdb_binaries})

add_library(chipdb-${family} OBJECT ${ICE40_CHIPDB} ${chipdb_sources})
add_dependencies(chipdb-${family} chipdb-${family}-bins)
target_compile_options(chipdb-${family} PRIVATE -g0 -O0 -w)
target_compile_definitions(chipdb-${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(chipdb-${family} PRIVATE ${family})
if(ICE40_DEVICES STREQUAL "1k")
    target_compile_definitions(chipdb-${family} PUBLIC ICE40_HX1K_ONLY=1)
endif()

foreach(family_target ${family_targets})
    target_sources(${family_target} PRIVATE $<TARGET_OBJECTS:chipdb-${family}>)
endforeach()
