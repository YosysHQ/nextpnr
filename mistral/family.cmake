set(MISTRAL_ROOT "" CACHE STRING "Mistral install path")

aux_source_directory(${MISTRAL_ROOT}/lib MISTRAL_LIB_FILES)
add_library(mistral STATIC ${MISTRAL_LIB_FILES})
target_compile_options(mistral PRIVATE -Wno-maybe-uninitialized -Wno-uninitialized -Wno-unknown-warning-option)

find_package(LibLZMA REQUIRED)

foreach(family_target ${family_targets})
    target_include_directories(${family_target} PRIVATE ${MISTRAL_ROOT}/lib ${LIBLZMA_INCLUDE_DIRS})
    target_link_libraries(${family_target} PRIVATE mistral ${LIBLZMA_LIBRARIES})
	# Currently required to avoid issues with mistral (LTO means the warnings can end up in nextpnr)
	target_link_options(${family_target} PRIVATE -Wno-maybe-uninitialized -Wno-uninitialized -Wno-unknown-warning-option)
endforeach()
