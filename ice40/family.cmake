set(devices 384 1k 5k 8k)
set(DB_PY ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdb.py)
foreach (dev ${devices})
    set(DEV_TXT_DB /usr/local/share/icebox/chipdb-${dev}.txt)
    set(DEV_CC_DB ${CMAKE_CURRENT_SOURCE_DIR}/ice40/chipdb-${dev}.cc)
    add_custom_command(OUTPUT ${DEV_CC_DB}
            COMMAND python3 ${DB_PY} ${DEV_TXT_DB} > ${DEV_CC_DB}.new
            COMMAND mv ${DEV_CC_DB}.new ${DEV_CC_DB}
            DEPENDS ${DEV_TXT_DB} ${DB_PY}
            )
    foreach (target ${family_targets})
        target_sources(${target} PRIVATE ${DEV_CC_DB})
    endforeach (target)
endforeach (dev)
