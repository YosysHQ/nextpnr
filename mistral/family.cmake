set(MISTRAL_ROOT "" CACHE STRING "Mistral install path")
set(MISTRAL_DONT_INSTALL ON)

add_subdirectory(${MISTRAL_ROOT}/tools ${CMAKE_CURRENT_BINARY_DIR}/tools)
add_subdirectory(${MISTRAL_ROOT}/generator ${CMAKE_CURRENT_BINARY_DIR}/generator)
add_subdirectory(${MISTRAL_ROOT}/libmistral ${CMAKE_CURRENT_BINARY_DIR}/libmistral)

find_package(LibLZMA REQUIRED)

foreach(family_target ${family_targets})
    target_include_directories(${family_target} PRIVATE ${MISTRAL_ROOT}/libmistral ${CMAKE_CURRENT_BINARY_DIR}/tools ${CMAKE_CURRENT_BINARY_DIR}/libmistral ${LIBLZMA_INCLUDE_DIRS})
    target_link_libraries(${family_target} PRIVATE mistral ${LIBLZMA_LIBRARIES})
endforeach()
