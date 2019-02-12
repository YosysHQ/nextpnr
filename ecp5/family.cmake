if (NOT EXTERNAL_CHIPDB)
    set(devices 25k 45k 85k)

    if (NOT DEFINED TRELLIS_ROOT)
        message(STATUS "TRELLIS_ROOT not defined using -DTRELLIS_ROOT=/path/to/prjtrellis. Default to /usr/local/share/trellis")
        set(TRELLIS_ROOT "/usr/local/share/trellis")
    endif()

    file(GLOB found_pytrellis ${TRELLIS_ROOT}/libtrellis/pytrellis.*
                              /usr/lib/pytrellis.*
                              /usr/lib64/pytrellis.*
                              /usr/lib/trellis/pytrellis.*
                              /usr/lib64/trellis/pytrellis.*)

    if ("${found_pytrellis}" STREQUAL "")
        message(FATAL_ERROR "failed to locate pytrellis library!")
    endif()

    list(GET found_pytrellis 0 PYTRELLIS_LIB)
    get_filename_component(PYTRELLIS_LIBDIR ${PYTRELLIS_LIB} DIRECTORY)

    set(DB_PY ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/trellis_import.py)

    file(MAKE_DIRECTORY ecp5/chipdbs/)
    add_library(ecp5_chipdb OBJECT ecp5/chipdbs/)
    target_compile_definitions(ecp5_chipdb PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
    target_include_directories(ecp5_chipdb PRIVATE ${family}/)

    if (CMAKE_HOST_WIN32)
    set(ENV_CMD ${CMAKE_COMMAND} -E env "PYTHONPATH=\"${PYTRELLIS_LIBDIR}\;${TRELLIS_ROOT}/util/common\;${TRELLIS_ROOT}/timing/util\"")
    else()
    set(ENV_CMD ${CMAKE_COMMAND} -E env "PYTHONPATH=${PYTRELLIS_LIBDIR}\:${TRELLIS_ROOT}/util/common:${TRELLIS_ROOT}/timing/util")
    endif()

    if (MSVC)
        target_sources(ecp5_chipdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/resource/embed.cc)
        set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ecp5/resources/chipdb.rc PROPERTIES LANGUAGE RC)
        foreach (dev ${devices})
            set(DEV_CC_DB ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/chipdbs/chipdb-${dev}.bin)
            set(DEV_CC_BBA_DB ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/chipdbs/chipdb-${dev}.bba)
        set(DEV_CONSTIDS_INC ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/constids.inc)
            add_custom_command(OUTPUT ${DEV_CC_BBA_DB}
            COMMAND ${ENV_CMD} python3 ${DB_PY} -p ${DEV_CONSTIDS_INC} ${dev} > ${DEV_CC_BBA_DB}
                    DEPENDS ${DB_PY}
                    )
            add_custom_command(OUTPUT ${DEV_CC_DB}
                    COMMAND bbasm ${DEV_CC_BBA_DB} ${DEV_CC_DB}
                    DEPENDS bbasm ${DEV_CC_BBA_DB}
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
            set(DEV_CC_BBA_DB ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/chipdbs/chipdb-${dev}.bba)
        set(DEV_CONSTIDS_INC ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/constids.inc)
            add_custom_command(OUTPUT ${DEV_CC_BBA_DB}
            COMMAND ${ENV_CMD} python3 ${DB_PY} -p ${DEV_CONSTIDS_INC} ${dev} > ${DEV_CC_BBA_DB}.new
                    COMMAND mv ${DEV_CC_BBA_DB}.new ${DEV_CC_BBA_DB}
                    DEPENDS ${DB_PY}
                    )
            add_custom_command(OUTPUT ${DEV_CC_DB}
                    COMMAND bbasm --c ${DEV_CC_BBA_DB} ${DEV_CC_DB}.new
                    COMMAND mv ${DEV_CC_DB}.new ${DEV_CC_DB}
                    DEPENDS bbasm ${DEV_CC_BBA_DB}
                    )
            target_sources(ecp5_chipdb PRIVATE ${DEV_CC_DB})
            foreach (target ${family_targets})
                target_sources(${target} PRIVATE $<TARGET_OBJECTS:ecp5_chipdb>)
            endforeach (target)
        endforeach (dev)
    endif()
endif()
