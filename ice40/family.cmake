if(ICE40_HX1K_ONLY)
    set(devices 1k)
    foreach (target ${family_targets})
        target_compile_definitions(${target} PRIVATE ICE40_HX1K_ONLY=1)
    endforeach (target)
else()
    set(devices 384 1k 5k 8k)
endif()

set(DB_PY ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdb.py)

set(ICEBOX_ROOT "/usr/local/share/icebox" CACHE STRING "icebox location root")
file(MAKE_DIRECTORY ice40/chipdbs/)
add_library(ice40_chipdb OBJECT ice40/chipdbs/)
target_compile_definitions(ice40_chipdb PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(ice40_chipdb PRIVATE ${family}/)

if (MSVC)
    target_sources(ice40_chipdb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ice40/resource/embed.cc)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ice40/resources/chipdb.rc PROPERTIES LANGUAGE RC)
    foreach (dev ${devices})
        if (dev EQUAL "5k")
            set(OPT_FAST "")
            set(OPT_SLOW --slow ${ICEBOX_ROOT}/timings_up5k.txt)
        elseif(dev EQUAL "384")
            set(OPT_FAST "")
            set(OPT_SLOW --slow ${ICEBOX_ROOT}/timings_lp384.txt)
        else()
            set(OPT_FAST --fast ${ICEBOX_ROOT}/timings_hx${dev}.txt)
            set(OPT_SLOW --slow ${ICEBOX_ROOT}/timings_lp${dev}.txt)
        endif()
        set(DEV_TXT_DB ${ICEBOX_ROOT}/chipdb-${dev}.txt)
        set(DEV_CC_BBA_DB ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdbs/chipdb-${dev}.bba)
        set(DEV_CC_DB ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdbs/chipdb-${dev}.bin)
        set(DEV_CONSTIDS_INC ${CMAKE_CURRENT_SOURCE_DIR}/ice40/constids.inc)
        set(DEV_GFXH ${CMAKE_CURRENT_SOURCE_DIR}/ice40/gfx.h)
        add_custom_command(OUTPUT ${DEV_CC_BBA_DB}
                COMMAND ${PYTHON_EXECUTABLE} ${DB_PY} -p ${DEV_CONSTIDS_INC} -g ${DEV_GFXH} ${OPT_FAST} ${OPT_SLOW} ${DEV_TXT_DB} > ${DEV_CC_BBA_DB}
                DEPENDS ${DEV_CONSTIDS_INC} ${DEV_GFXH} ${DEV_TXT_DB} ${DB_PY}
                )
        add_custom_command(OUTPUT ${DEV_CC_DB}
                COMMAND bbasm ${DEV_CC_BBA_DB} ${DEV_CC_DB}
                DEPENDS bbasm ${DEV_CC_BBA_DB}
        )
        target_sources(ice40_chipdb PRIVATE ${DEV_CC_DB})
        set_source_files_properties(${DEV_CC_DB} PROPERTIES HEADER_FILE_ONLY TRUE)
        foreach (target ${family_targets})
            target_sources(${target} PRIVATE $<TARGET_OBJECTS:ice40_chipdb> ${CMAKE_CURRENT_SOURCE_DIR}/ice40/resource/chipdb.rc)
        endforeach (target)
    endforeach (dev)
else()
    target_compile_options(ice40_chipdb PRIVATE -g0 -O0 -w)
    foreach (dev ${devices})
        if (dev EQUAL "5k")
            set(OPT_FAST "")
            set(OPT_SLOW --slow ${ICEBOX_ROOT}/timings_up5k.txt)
        elseif(dev EQUAL "384")
            set(OPT_FAST "")
            set(OPT_SLOW --slow ${ICEBOX_ROOT}/timings_lp384.txt)
        else()
            set(OPT_FAST --fast ${ICEBOX_ROOT}/timings_hx${dev}.txt)
            set(OPT_SLOW --slow ${ICEBOX_ROOT}/timings_lp${dev}.txt)
        endif()
        set(DEV_TXT_DB ${ICEBOX_ROOT}/chipdb-${dev}.txt)
        set(DEV_CC_BBA_DB ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdbs/chipdb-${dev}.bba)
        set(DEV_CC_DB ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdbs/chipdb-${dev}.cc)
        set(DEV_CONSTIDS_INC ${CMAKE_CURRENT_SOURCE_DIR}/ice40/constids.inc)
        set(DEV_GFXH ${CMAKE_CURRENT_SOURCE_DIR}/ice40/gfx.h)
        add_custom_command(OUTPUT ${DEV_CC_BBA_DB}
                COMMAND ${PYTHON_EXECUTABLE} ${DB_PY} -p ${DEV_CONSTIDS_INC} -g ${DEV_GFXH} ${OPT_FAST} ${OPT_SLOW} ${DEV_TXT_DB} > ${DEV_CC_BBA_DB}.new
                COMMAND mv ${DEV_CC_BBA_DB}.new ${DEV_CC_BBA_DB}
                DEPENDS ${DEV_CONSTIDS_INC} ${DEV_GFXH} ${DEV_TXT_DB} ${DB_PY}
        )
        add_custom_command(OUTPUT ${DEV_CC_DB}
                COMMAND bbasm --c ${DEV_CC_BBA_DB} ${DEV_CC_DB}.new
                COMMAND mv ${DEV_CC_DB}.new ${DEV_CC_DB}
                DEPENDS bbasm ${DEV_CC_BBA_DB}
        )
        target_sources(ice40_chipdb PRIVATE ${DEV_CC_DB})
        foreach (target ${family_targets})
            target_sources(${target} PRIVATE $<TARGET_OBJECTS:ice40_chipdb>)
        endforeach (target)
    endforeach (dev)
endif()
