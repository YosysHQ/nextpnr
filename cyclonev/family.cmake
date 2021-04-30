set(MISTRAL_ROOT "" CACHE STRING "Mistral install path")

foreach(family_target ${family_targets})
    target_include_directories(${family_target} PRIVATE ${MISTRAL_ROOT}/lib)
endforeach()
