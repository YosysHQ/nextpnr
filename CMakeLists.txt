# TODO: sensible minimum CMake version
cmake_minimum_required(VERSION 3.3)
project(nextpnr)

option(BUILD_GUI "Build GUI" ON)
option(BUILD_PYTHON "Build Python Integration" ON)
option(BUILD_TESTS "Build GUI" OFF)

# List of families to build
set(FAMILIES dummy ice40)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS_DEBUG "-Wall -fPIC -ggdb")
set(CMAKE_CXX_FLAGS_RELEASE "-Wall -fPIC -O3 -g")
set(CMAKE_DEFIN)
# List of Boost libraries to include
set(boost_libs filesystem thread program_options)

if (BUILD_PYTHON)
    # TODO: sensible minimum Python version
    find_package(PythonInterp 3.5 REQUIRED)
    find_package(PythonLibs 3.5 REQUIRED)
else()
    add_definitions("-DNO_PYTHON")
endif()

find_package(Boost REQUIRED COMPONENTS ${boost_libs})

if (BUILD_GUI)
    # Find the Qt5 libraries
    find_package(Qt5 COMPONENTS Core Widgets OpenGL REQUIRED)
    find_package(OpenGL REQUIRED)
else()
    add_definitions("-DNO_GUI")
endif()

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (BUILD_TESTS)
    add_subdirectory(3rdparty/googletest/googletest generated/3rdparty/googletest EXCLUDE_FROM_ALL)
    enable_testing()
endif()

if (BUILD_GUI)
    add_subdirectory(3rdparty/QtPropertyBrowser generated/3rdparty/QtPropertyBrowser)
endif()

add_definitions("-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/common/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
)

if (BUILD_PYTHON)
    # Find Boost::Python of a suitable version in a cross-platform way
    # Some distributions (Arch) call it libboost_python3, others such as Ubuntu
    # call it libboost_python35. In the latter case we must consider all minor versions
    # Original source: https://github.com/BVLC/caffe/blob/master/cmake/Dependencies.cmake#L148
    set(version ${PYTHONLIBS_VERSION_STRING})

    STRING(REGEX REPLACE "[^0-9]" "" boost_py_version ${version})
    find_package(Boost COMPONENTS "python-py${boost_py_version}" ${boost_libs})
    set(Boost_PYTHON_FOUND ${Boost_PYTHON-PY${boost_py_version}_FOUND})

    while (NOT "${version}" STREQUAL "" AND NOT Boost_PYTHON_FOUND)
        STRING(REGEX REPLACE "([0-9.]+).[0-9]+" "\\1" version ${version})

        STRING(REGEX REPLACE "[^0-9]" "" boost_py_version ${version})
        find_package(Boost COMPONENTS "python-py${boost_py_version}" ${boost_libs})
        set(Boost_PYTHON_FOUND ${Boost_PYTHON-PY${boost_py_version}_FOUND})

        STRING(REGEX MATCHALL "([0-9.]+).[0-9]+" has_more_version ${version})
        if ("${has_more_version}" STREQUAL "")
            break()
        endif ()
    endwhile ()

    if (NOT Boost_PYTHON_FOUND)
        find_package(Boost COMPONENTS python3 ${boost_libs})
        if ("${Boost_LIBRARIES}" MATCHES ".*(python|PYTHON).*" )
            set(Boost_PYTHON_FOUND TRUE)
        endif ()
    endif ()

    if (NOT Boost_PYTHON_FOUND)
        STRING(REGEX REPLACE "([0-9]+\\.[0-9]+).*" "\\1" gentoo_version ${PYTHONLIBS_VERSION_STRING})
        find_package(Boost COMPONENTS python-${gentoo_version} ${boost_libs})
        if ("${Boost_LIBRARIES}" MATCHES ".*(python|PYTHON).*" )
            set(Boost_PYTHON_FOUND TRUE)
        endif ()
    endif ()

    if (NOT Boost_PYTHON_FOUND )
        message( FATAL_ERROR "No version of Boost::Python 3.x could be found.")
    endif ()
endif()

include_directories(common/ frontend/json ${Boost_INCLUDE_DIRS} ${PYTHON_INCLUDE_DIRS})
aux_source_directory(common/ COMMON_SRC_FILES)
aux_source_directory(frontend/json/ JSON_PARSER_FILES)
set(COMMON_FILES ${COMMON_SRC_FILES} ${JSON_PARSER_FILES})
set(CMAKE_BUILD_TYPE Release)

if(MINGW)
    add_definitions("-Wa,-mbig-obj")
endif(MINGW)

foreach (family ${FAMILIES})
    string(TOUPPER ${family} ufamily)
    aux_source_directory(${family}/ ${ufamily}_FILES)    

    if (BUILD_GUI)
       add_subdirectory(gui generated/gui/${family})
    endif()

    # Add the CLI binary target
    add_executable(nextpnr-${family} ${COMMON_FILES} ${${ufamily}_FILES} )
    install(TARGETS nextpnr-${family} RUNTIME DESTINATION bin)
    target_compile_definitions(nextpnr-${family} PRIVATE MAIN_EXECUTABLE)

    if (BUILD_PYTHON)
        # Add the importable Python module target
        PYTHON_ADD_MODULE(nextpnrpy_${family} EXCLUDE_FROM_ALL ${COMMON_FILES} ${${ufamily}_FILES})
    endif()

    # Add any new per-architecture targets here
    if (BUILD_TESTS)
        aux_source_directory(tests/${family}/ ${ufamily}_TEST_FILES)

        add_executable(nextpnr-${family}-test ${${ufamily}_TEST_FILES} ${COMMON_FILES} ${${ufamily}_FILES})
        target_link_libraries(nextpnr-${family}-test PRIVATE gtest_main)

        add_test(${family}-test ${CMAKE_CURRENT_BINARY_DIR}/nextpnr-${family}-test)
    endif()

    # Set ${family_targets} to the list of targets being build for this family
    set(family_targets nextpnr-${family})
    
    if (BUILD_TESTS)
        set(family_targets ${family_targets} nextpnr-${family}-test)
    endif()

    if (BUILD_PYTHON)
        set(family_targets ${family_targets} nextpnrpy_${family})
    endif()

    # Include the family-specific CMakeFile
    include(${family}/family.cmake)
    foreach (target ${family_targets})
        # Include family-specific source files to all family targets and set defines appropriately
        target_include_directories(${target} PRIVATE ${family}/ generated/)
        target_compile_definitions(${target} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family} ARCH_${ufamily} ARCHNAME=${family})
        target_link_libraries(${target} LINK_PUBLIC ${Boost_LIBRARIES})
        if (BUILD_PYTHON)
            target_link_libraries(${target} LINK_PUBLIC ${PYTHON_LIBRARIES})
        endif()
        if (BUILD_GUI)
            target_include_directories(${target} PRIVATE gui/${family}/ gui/)
            target_compile_definitions(${target} PRIVATE QT_NO_KEYWORDS)
            target_link_libraries(${target} LINK_PUBLIC gui_${family} ${GUI_LIBRARY_FILES_${ufamily}})
        endif()
    endforeach (target)
endforeach (family)

file(GLOB_RECURSE CLANGFORMAT_FILES *.cc *.h)
string(REGEX REPLACE "[^;]*/ice40/chipdbs/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/3rdparty[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/generated[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")

add_custom_target(
    clangformat
    COMMAND clang-format
    -style=file
    -i
    ${CLANGFORMAT_FILES}
)

unset(BUILD_GUI CACHE)
unset(BUILD_PYTHON CACHE)
unset(BUILD_TESTS CACHE)
