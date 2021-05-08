set(MISTRAL_ROOT "" CACHE STRING "Mistral install path")

aux_source_directory(${MISTRAL_ROOT}/lib MISTRAL_LIB_FILES)
add_library(mistral STATIC ${MISTRAL_LIB_FILES})

find_package(LibLZMA REQUIRED)

foreach(family_target ${family_targets})
    target_include_directories(${family_target} PRIVATE ${MISTRAL_ROOT}/lib ${LIBLZMA_INCLUDE_DIRS})
    target_link_libraries(${family_target} PRIVATE mistral ${LIBLZMA_LIBRARIES})
endforeach()
