cmake_minimum_required(VERSION 3.25)
project(bba CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS
    program_options)

add_executable(bbasm
    main.cc)
target_link_libraries(bbasm LINK_PRIVATE
    ${Boost_PROGRAM_OPTIONS_LIBRARY})
export(TARGETS bbasm FILE ${CMAKE_BINARY_DIR}/bba-export.cmake)
