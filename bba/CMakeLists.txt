cmake_minimum_required(VERSION 3.25)
project(bba CXX)

set(CMAKE_CXX_STANDARD 17)

set(boost_libs program_options iostreams)

find_package(Boost REQUIRED COMPONENTS ${boost_libs})

if (Boost_VERSION VERSION_LESS "1.89.0")
    list(APPEND boost_libs system)
    find_package(Boost REQUIRED COMPONENTS ${boost_libs})
endif()


add_executable(bbasm
    main.cc)

set(boost_link_libs ${Boost_PROGRAM_OPTIONS_LIBRARY})
if(Boost_VERSION VERSION_LESS "1.89.0")
    list(APPEND boost_link_libs ${Boost_SYSTEM_LIBRARY})
endif()

target_link_libraries(bbasm LINK_PRIVATE ${boost_link_libs})

export(TARGETS bbasm FILE ${CMAKE_BINARY_DIR}/bba-export.cmake)
