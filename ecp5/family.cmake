if (NOT EXTERNAL_CHIPDB)
    set(devices 25k 45k 85k)

    if (NOT DEFINED TRELLIS_INSTALL_PREFIX)
        message(STATUS "TRELLIS_INSTALL_PREFIX not defined using -DTRELLIS_INSTALL_PREFIX=/path-prefix/to/prjtrellis-installation. Defaulted to ${CMAKE_INSTALL_PREFIX}")
        set(TRELLIS_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
    endif()

    if (NOT DEFINED PYTRELLIS_LIBDIR)
        find_library(PYTRELLIS pytrellis.so
            PATHS ${TRELLIS_INSTALL_PREFIX}/lib/trellis
            PATH_SUFFIXES trellis
            DOC "Location of pytrellis library")

        if ("${PYTRELLIS}" STREQUAL "PYTRELLIS-NOTFOUND")
            message(FATAL_ERROR "Failed to locate pytrellis library!")
        endif()

        get_filename_component(PYTRELLIS_LIBDIR ${PYTRELLIS} DIRECTORY)
    endif()

    set(DB_PY ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/trellis_import.py)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ecp5/chipdbs/)
    add_library(ecp5_chipdb OBJECT ${CMAKE_CURRENT_BINARY_DIR}/ecp5/chipdbs/)
    target_compile_definitions(ecp5_chipdb PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
    target_include_directories(ecp5_chipdb PRIVATE ${family}/)

    if (CMAKE_HOST_WIN32)
        set(ENV_CMD ${CMAKE_COMMAND} -E env "PYTHONPATH=\"${PYTRELLIS_LIBDIR}\;${TRELLIS_INSTALL_PREFIX}/share/trellis/util/common\;${TRELLIS_INSTALL_PREFIX}/share/trellis/timing/util\"")
    else()
        set(ENV_CMD ${CMAKE_COMMAND} -E env "PYTHONPATH=${PYTRELLIS_LIBDIR}\:${TRELLIS_INSTALL_PREFIX}/share/trellis/util/common:${TRELLIS_INSTALL_PREFIX}/share/trellis/timing/util")
    endif()

    if (MSVC)
        target_sources(ecp5_chipdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/resource/embed.cc)
        set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ecp5/resources/chipdb.rc PROPERTIES LANGUAGE RC)
        set(PREV_DEV_CC_BBA_DB)
        foreach (dev ${devices})
            set(DEV_CC_DB ${CMAKE_CURRENT_BINARY_DIR}/ecp5/chipdbs/chipdb-${dev}.bin)
            set(DEV_CC_BBA_DB ${CMAKE_CURRENT_BINARY_DIR}/ecp5/chipdbs/chipdb-${dev}.bba)
            set(DEV_CONSTIDS_INC ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/constids.inc)
            set(DEV_GFXH ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/gfx.h)
            if (PREGENERATED_BBA_PATH)
                add_custom_command(OUTPUT ${DEV_CC_DB}
                    COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${PREGENERATED_BBA_PATH}/chipdb-${dev}.bba ${DEV_CC_DB}
                    )
            else()
                add_custom_command(OUTPUT ${DEV_CC_BBA_DB}
                    COMMAND ${ENV_CMD} ${PYTHON_EXECUTABLE} ${DB_PY} -p ${DEV_CONSTIDS_INC} -g ${DEV_GFXH} ${dev} > ${DEV_CC_BBA_DB}
                    DEPENDS ${DB_PY} ${DEV_CONSTIDS_INC} ${DEV_GFXH} ${PREV_DEV_CC_BBA_DB}
                    )
                add_custom_command(OUTPUT ${DEV_CC_DB}
                    COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${DEV_CC_BBA_DB} ${DEV_CC_DB}
                    DEPENDS bbasm ${DEV_CC_BBA_DB}
                    )
            endif()
            if (SERIALIZE_CHIPDB)
                set(PREV_DEV_CC_BBA_DB ${DEV_CC_BBA_DB})
            endif()
            target_sources(ecp5_chipdb PRIVATE ${DEV_CC_DB})
            set_source_files_properties(${DEV_CC_DB} PROPERTIES HEADER_FILE_ONLY TRUE)
            foreach (target ${family_targets})
                target_sources(${target} PRIVATE $<TARGET_OBJECTS:ecp5_chipdb> ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/resource/chipdb.rc)
            endforeach()
        endforeach()
    else()
        target_compile_options(ecp5_chipdb PRIVATE -g0 -O0 -w)
        set(PREV_DEV_CC_BBA_DB)
        foreach (dev ${devices})
            set(DEV_CC_DB ${CMAKE_CURRENT_BINARY_DIR}/ecp5/chipdbs/chipdb-${dev}.cc)
            set(DEV_CC_BBA_DB ${CMAKE_CURRENT_BINARY_DIR}/ecp5/chipdbs/chipdb-${dev}.bba)
            set(DEV_CONSTIDS_INC ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/constids.inc)
            set(DEV_GFXH ${CMAKE_CURRENT_SOURCE_DIR}/ecp5/gfx.h)
            if (PREGENERATED_BBA_PATH)
                add_custom_command(OUTPUT ${DEV_CC_DB}
                    COMMAND bbasm --c ${BBASM_ENDIAN_FLAG} ${PREGENERATED_BBA_PATH}/chipdb-${dev}.bba ${DEV_CC_DB}.new
                    COMMAND mv ${DEV_CC_DB}.new ${DEV_CC_DB}
                    )
            else()
                add_custom_command(OUTPUT ${DEV_CC_BBA_DB}
                    COMMAND ${ENV_CMD} ${PYTHON_EXECUTABLE} ${DB_PY} -p ${DEV_CONSTIDS_INC} -g ${DEV_GFXH} ${dev} > ${DEV_CC_BBA_DB}.new
                    COMMAND mv ${DEV_CC_BBA_DB}.new ${DEV_CC_BBA_DB}
                    DEPENDS ${DB_PY} ${DEV_CONSTIDS_INC} ${DEV_GFXH} ${PREV_DEV_CC_BBA_DB}
                    )
                add_custom_command(OUTPUT ${DEV_CC_DB}
                    COMMAND bbasm --c ${BBASM_ENDIAN_FLAG} ${DEV_CC_BBA_DB} ${DEV_CC_DB}.new
                    COMMAND mv ${DEV_CC_DB}.new ${DEV_CC_DB}
                    DEPENDS bbasm ${DEV_CC_BBA_DB}
                    )
            endif()
            if (SERIALIZE_CHIPDB)
                set(PREV_DEV_CC_BBA_DB ${DEV_CC_BBA_DB})
            endif()
            target_sources(ecp5_chipdb PRIVATE ${DEV_CC_DB})
            foreach (target ${family_targets})
                target_sources(${target} PRIVATE $<TARGET_OBJECTS:ecp5_chipdb>)
            endforeach()
        endforeach()
    endif()
endif()
