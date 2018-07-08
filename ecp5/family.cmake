
set(devices 25k)

set(DB_PY ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/trellis_import.py)

file(MAKE_DIRECTORY ecp5/chipdbs/)
add_library(ecp5_chipdb OBJECT ecp5/chipdbs/)
target_compile_definitions(ecp5_chipdb PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(ecp5_chipdb PRIVATE ${family}/)
set(ENV_CMD ${CMAKE_COMMAND} -E env "PYTHONPATH=${TRELLIS_ROOT}/libtrellis:${TRELLIS_ROOT}/util/common")
if (MSVC)
    target_sources(ecp5_chipdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/resource/embed.cc)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ecp5/resources/chipdb.rc PROPERTIES LANGUAGE RC)
    foreach (dev ${devices})
        set(DEV_CC_DB ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/chipdbs/chipdb-${dev}.bin)
        set(DEV_PORTS_INC ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/portpins.inc)
        add_custom_command(OUTPUT ${DEV_CC_DB}
                COMMAND ${ENV_CMD} python3 ${DB_PY} -b -p ${DEV_PORTS_INC} ${dev} ${DEV_CC_DB}
                DEPENDS ${DB_PY}
                )
        target_sources(ecp5_chipdb PRIVATE ${DEV_CC_DB})
        set_source_files_properties(${DEV_CC_DB} PROPERTIES HEADER_FILE_ONLY TRUE)
        foreach (target ${family_targets})
            target_sources(${target} PRIVATE $<TARGET_OBJECTS:ecp5_chipdb> ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/resource/chipdb.rc)
        endforeach (target)
    endforeach (dev)
else()
    target_compile_options(ecp5_chipdb PRIVATE -g0 -O0 -w)
    foreach (dev ${devices})
        set(DEV_CC_DB ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/chipdbs/chipdb-${dev}.cc)
        set(DEV_PORTS_INC ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/portpins.inc)
        add_custom_command(OUTPUT ${DEV_CC_DB}
                COMMAND ${ENV_CMD} python3 ${DB_PY} -c -p ${DEV_PORTS_INC} ${dev} ${DEV_CC_DB}
                DEPENDS ${DB_PY}
                )
        target_sources(ecp5_chipdb PRIVATE ${DEV_CC_DB})
        foreach (target ${family_targets})
            target_sources(${target} PRIVATE $<TARGET_OBJECTS:ecp5_chipdb>)
        endforeach (target)
    endforeach (dev)
endif()

find_library(TRELLIS_LIB trellis PATHS ${TRELLIS_ROOT}/libtrellis)

foreach (target ${family_targets})
    target_compile_definitions(${target} PRIVATE TRELLIS_ROOT="${TRELLIS_ROOT}")
    target_include_directories(${target} PRIVATE ${TRELLIS_ROOT}/libtrellis/include)
    target_link_libraries(${target} PRIVATE ${TRELLIS_LIB})
endforeach (target)
