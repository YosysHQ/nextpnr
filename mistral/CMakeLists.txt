set(SOURCES
    arch.cc
    archdefs.cc
    archdefs.h
    arch.h
    arch_pybindings.cc
    arch_pybindings.h
    bitstream.cc
    constids.inc
    delay.cc
    globals.cc
    io.cc
    lab.cc
    m10k.cc
    pack.cc
    pins.cc
    qsf.cc
)

add_nextpnr_architecture(${family}
    CORE_SOURCES ${SOURCES}
    MAIN_SOURCE  main.cc
)

set(MISTRAL_ROOT "" CACHE STRING "Mistral install path")
set(MISTRAL_DONT_INSTALL ON)

add_subdirectory(${MISTRAL_ROOT}/tools ${CMAKE_CURRENT_BINARY_DIR}/tools)
add_subdirectory(${MISTRAL_ROOT}/generator ${CMAKE_CURRENT_BINARY_DIR}/generator)
add_subdirectory(${MISTRAL_ROOT}/libmistral ${CMAKE_CURRENT_BINARY_DIR}/libmistral)

find_package(LibLZMA REQUIRED)

target_include_directories(nextpnr-${family}-core INTERFACE
    ${MISTRAL_ROOT}/libmistral
    ${CMAKE_CURRENT_BINARY_DIR}/tools
    ${CMAKE_CURRENT_BINARY_DIR}/libmistral
    ${LIBLZMA_INCLUDE_DIRS}
)

target_link_libraries(nextpnr-${family}-core INTERFACE
    mistral
    ${LIBLZMA_LIBRARIES}
)
