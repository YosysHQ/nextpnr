cmake_minimum_required(VERSION 3.5)
project(bba CXX)

find_package(Boost REQUIRED COMPONENTS
    program_options
    filesystem
    system)

add_executable(bbasm
    main.cc)
target_link_libraries(bbasm LINK_PRIVATE
    ${Boost_PROGRAM_OPTIONS_LIBRARY}
    ${Boost_FILESYSTEM_LIBRARY}
    ${Boost_SYSTEM_LIBRARY})
export(TARGETS bbasm FILE ${CMAKE_BINARY_DIR}/bba-export.cmake)
