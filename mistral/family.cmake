set(MISTRAL_ROOT "" CACHE STRING "Mistral install path")
set(MISTRAL_DONT_INSTALL ON)

add_subdirectory(${MISTRAL_ROOT}/libmistral ${CMAKE_CURRENT_BINARY_DIR}/libmistral)

find_package(LibLZMA REQUIRED)

foreach(family_target ${family_targets})
    target_include_directories(${family_target} PRIVATE ${MISTRAL_ROOT}/libmistral ${LIBLZMA_INCLUDE_DIRS})
    target_link_libraries(${family_target} PRIVATE mistral ${LIBLZMA_LIBRARIES})
	# Currently required to avoid issues with mistral (LTO means the warnings can end up in nextpnr)
	target_link_options(${family_target} PRIVATE -Wno-maybe-uninitialized -Wno-uninitialized -Wno-unknown-warning-option)
endforeach()
