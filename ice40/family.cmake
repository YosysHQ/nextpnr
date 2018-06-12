if(ICE40_HX1K_ONLY)
    set(devices 1k)
    foreach (target ${family_targets})
        target_compile_definitions(${target} PRIVATE ICE40_HX1K_ONLY=1)
    endforeach (target)
else()
    set(devices 384 1k 5k 8k)
endif()

set(DB_PY ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdb.py)
file(MAKE_DIRECTORY ice40/chipdbs/)
add_library(ice40_chipdb OBJECT ice40/chipdbs/)
target_compile_options(ice40_chipdb PRIVATE -g0 -O0 -w)
target_include_directories(ice40_chipdb PRIVATE ${family}/)
foreach (dev ${devices})
    set(DEV_TXT_DB /usr/local/share/icebox/chipdb-${dev}.txt)
    set(DEV_CC_DB ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdbs/chipdb-${dev}.cc)
    add_custom_command(OUTPUT ${DEV_CC_DB}
            COMMAND python3 ${DB_PY} ${DEV_TXT_DB} > ${DEV_CC_DB}.new
            COMMAND mv ${DEV_CC_DB}.new ${DEV_CC_DB}
            DEPENDS ${DEV_TXT_DB} ${DB_PY}
            )
    target_sources(ice40_chipdb PRIVATE ${DEV_CC_DB})
    foreach (target ${family_targets})
        target_sources(${target} PRIVATE $<TARGET_OBJECTS:ice40_chipdb>)
    endforeach (target)
endforeach (dev)
