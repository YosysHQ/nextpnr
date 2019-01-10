# TODO: sensible minimum CMake version
cmake_minimum_required(VERSION 3.3)
project(nextpnr)

option(BUILD_GUI "Build GUI" ON)
option(BUILD_PYTHON "Build Python Integration" ON)
option(BUILD_TESTS "Build GUI" OFF)
option(COVERAGE "Add code coverage info" OFF)
option(STATIC_BUILD "Create static build" OFF)
option(EXTERNAL_CHIPDB "Create build with pre-built chipdb binaries" OFF)

set(link_param "")
if (STATIC_BUILD)
    set(Boost_USE_STATIC_LIBS   ON)
    if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set(link_param "-static")
    endif()
endif()

if (EXTERNAL_CHIPDB)
    if (NOT DEFINED EXTERNAL_CHIPDB_ROOT)
        message(STATUS "EXTERNAL_CHIPDB_ROOT not defined using -DEXTERNAL_CHIPDB_ROOT=/path/to/nextpnr. Default to /usr/share/nextpnr")
        set(EXTERNAL_CHIPDB_ROOT "/usr/share/nextpnr")
    endif()
    add_definitions("-DEXTERNAL_CHIPDB_ROOT=\"${EXTERNAL_CHIPDB_ROOT}\"")
endif()

# List of families to build
set(FAMILIES generic ice40 ecp5)

set(ARCH "" CACHE STRING "Architecture family for nextpnr build")
set_property(CACHE ARCH PROPERTY STRINGS ${FAMILIES})

if (NOT ARCH)
    message(STATUS "Architecture needs to be set, set desired one with -DARCH=xxx")
    message(STATUS "Supported architectures are :")
    message(STATUS "  all")
    foreach(item ${FAMILIES})
        message(STATUS "  ${item}")
    endforeach()
    message(FATAL_ERROR "Architecture setting is mandatory")
endif ()

if (ARCH STREQUAL "all")
    SET(ARCH ${FAMILIES})
endif()

foreach(item ${ARCH})
    if (NOT item IN_LIST FAMILIES)
        message(FATAL_ERROR "Architecture '${item}' not in list of supported architectures")
    endif()
endforeach()

set(CMAKE_CXX_STANDARD 11)
if (MSVC)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_DEBUG /W4 /wd4100 /wd4244 /wd4125 /wd4800 /wd4456 /wd4458 /wd4305 /wd4459 /wd4121 /wd4996")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /W4 /wd4100 /wd4244 /wd4125 /wd4800 /wd4456 /wd4458 /wd4305 /wd4459 /wd4121 /wd4996 /wd4127")
else()
set(CMAKE_CXX_FLAGS_DEBUG "-Wall -fPIC -ggdb -pipe")
set(CMAKE_CXX_FLAGS_RELEASE "-Wall -fPIC -O3 -g -pipe")
endif()
set(CMAKE_DEFIN)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/3rdparty/sanitizers-cmake/cmake;." ${CMAKE_MODULE_PATH})

if (COVERAGE)
    include(CodeCoverage)
endif()

if(NOT DEFINED CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
     set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "No dev warnings")
endif()

find_package(Sanitizers)

# List of Boost libraries to include
set(boost_libs filesystem thread program_options iostreams)

if (BUILD_GUI AND NOT BUILD_PYTHON)
    message(FATAL_ERROR "GUI requires Python to build")
endif()

find_package(PythonInterp 3.5 REQUIRED)
if (BUILD_PYTHON)
    # TODO: sensible minimum Python version
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
    add_subdirectory(3rdparty/googletest/googletest ${CMAKE_CURRENT_BINARY_DIR}/generated/3rdparty/googletest EXCLUDE_FROM_ALL)
    enable_testing()
endif()

if (BUILD_GUI)
    add_subdirectory(3rdparty/QtPropertyBrowser ${CMAKE_CURRENT_BINARY_DIR}/generated/3rdparty/QtPropertyBrowser EXCLUDE_FROM_ALL)
endif()

add_definitions("-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH}")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/common/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
)

add_subdirectory(3rdparty/taucs ${CMAKE_CURRENT_BINARY_DIR}/generated/taucs EXCLUDE_FROM_ALL)

if (BUILD_PYTHON)
    # Find Boost::Python of a suitable version in a cross-platform way
    # Some distributions (Arch) call it libboost_python3, others such as Ubuntu
    # call it libboost_python35. In the latter case we must consider all minor versions
    # Original source: https://github.com/BVLC/caffe/blob/master/cmake/Dependencies.cmake#L148
    set(version ${PYTHONLIBS_VERSION_STRING})

    STRING(REGEX REPLACE "[^0-9]" "" boost_py_version ${version})
    find_package(Boost QUIET COMPONENTS "python-py${boost_py_version}" ${boost_libs})
    set(Boost_PYTHON_FOUND ${Boost_PYTHON-PY${boost_py_version}_FOUND})

    while (NOT "${version}" STREQUAL "" AND NOT Boost_PYTHON_FOUND)
        STRING(REGEX REPLACE "([0-9.]+).[0-9]+" "\\1" version ${version})

        STRING(REGEX REPLACE "[^0-9]" "" boost_py_version ${version})
        find_package(Boost QUIET COMPONENTS "python-py${boost_py_version}" ${boost_libs})
        set(Boost_PYTHON_FOUND ${Boost_PYTHON-PY${boost_py_version}_FOUND})

        STRING(REGEX MATCHALL "([0-9.]+).[0-9]+" has_more_version ${version})
        if ("${has_more_version}" STREQUAL "")
            break()
        endif ()
    endwhile ()

    if (NOT Boost_PYTHON_FOUND)
        find_package(Boost QUIET COMPONENTS python3 ${boost_libs})
        if ("${Boost_LIBRARIES}" MATCHES ".*(python|PYTHON).*" )
            set(Boost_PYTHON_FOUND TRUE)
        endif ()
    endif ()

    if (NOT Boost_PYTHON_FOUND)
        find_package(Boost QUIET COMPONENTS python36 ${boost_libs})
        if ("${Boost_LIBRARIES}" MATCHES ".*(python|PYTHON).*" )
            set(Boost_PYTHON_FOUND TRUE)
        endif ()
    endif ()

    if (NOT Boost_PYTHON_FOUND)
        find_package(Boost QUIET COMPONENTS python37 ${boost_libs})
        if ("${Boost_LIBRARIES}" MATCHES ".*(python|PYTHON).*" )
            set(Boost_PYTHON_FOUND TRUE)
        endif ()
    endif ()

    if (NOT Boost_PYTHON_FOUND)
        STRING(REGEX REPLACE "([0-9]+\\.[0-9]+).*" "\\1" gentoo_version ${PYTHONLIBS_VERSION_STRING})
        find_package(Boost QUIET COMPONENTS python-${gentoo_version} ${boost_libs})
        if ("${Boost_LIBRARIES}" MATCHES ".*(python|PYTHON).*" )
            set(Boost_PYTHON_FOUND TRUE)
        endif ()
    endif ()

    if (NOT Boost_PYTHON_FOUND )
        message( FATAL_ERROR "No version of Boost::Python 3.x could be found.")
    endif ()
endif()

include_directories(common/ json/ ${Boost_INCLUDE_DIRS} ${PYTHON_INCLUDE_DIRS} 3rdparty/taucs/src ${CMAKE_CURRENT_BINARY_DIR}/generated/taucs/build/linux)
aux_source_directory(common/ COMMON_SRC_FILES)
aux_source_directory(json/ JSON_PARSER_FILES)
set(COMMON_FILES ${COMMON_SRC_FILES} ${JSON_PARSER_FILES})
set(CMAKE_BUILD_TYPE Release)

if(MINGW)
    add_definitions("-Wa,-mbig-obj")
endif(MINGW)

include(bba/bba.cmake)

foreach (family ${ARCH})
    message(STATUS "Configuring architecture : ${family}")
    string(TOUPPER ${family} ufamily)
    aux_source_directory(${family}/ ${ufamily}_FILES)    

    if (BUILD_GUI)
       add_subdirectory(gui ${CMAKE_CURRENT_BINARY_DIR}/generated/gui/${family} EXCLUDE_FROM_ALL)
    endif()

    # Add the CLI binary target
    add_executable(nextpnr-${family} ${COMMON_FILES} ${${ufamily}_FILES})
    install(TARGETS nextpnr-${family} RUNTIME DESTINATION bin)
    target_compile_definitions(nextpnr-${family} PRIVATE MAIN_EXECUTABLE)
        
    # Add any new per-architecture targets here
    if (BUILD_TESTS)
        if (COVERAGE)
            APPEND_COVERAGE_COMPILER_FLAGS()
            set(COVERAGE_LCOV_EXCLUDES '/usr/include/*' '3rdparty/*' 'generated/*' 'bba/*' 'tests/*')
            SETUP_TARGET_FOR_COVERAGE_LCOV(
                NAME ${family}-coverage 
                EXECUTABLE nextpnr-${family}-test
                DEPENDENCIES nextpnr-${family}-test
            )
        endif()

        aux_source_directory(tests/${family}/ ${ufamily}_TEST_FILES)
        if (BUILD_GUI)
            aux_source_directory(tests/gui/ GUI_TEST_FILES)
        endif()

        add_executable(nextpnr-${family}-test ${${ufamily}_TEST_FILES}
                ${COMMON_FILES} ${${ufamily}_FILES} ${GUI_TEST_FILES})
        target_link_libraries(nextpnr-${family}-test PRIVATE gtest_main)
        add_sanitizers(nextpnr-${family}-test)

        add_test(${family}-test ${CMAKE_CURRENT_BINARY_DIR}/nextpnr-${family}-test)
    endif()

    # Set ${family_targets} to the list of targets being build for this family
    set(family_targets nextpnr-${family})
    
    if (BUILD_TESTS)
        set(family_targets ${family_targets} nextpnr-${family}-test)
    endif()

    # Include the family-specific CMakeFile
    include(${family}/family.cmake)
    foreach (target ${family_targets})
        # Include family-specific source files to all family targets and set defines appropriately
        target_include_directories(${target} PRIVATE ${family}/ ${CMAKE_CURRENT_BINARY_DIR}/generated/)
        target_compile_definitions(${target} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family} ARCH_${ufamily} ARCHNAME=${family})
        target_link_libraries(${target} LINK_PUBLIC ${Boost_LIBRARIES} taucs ${link_param})
        if (NOT MSVC)
            target_link_libraries(${target} LINK_PUBLIC pthread)
        endif()
        add_sanitizers(${target})
        if (BUILD_GUI)
            target_include_directories(${target} PRIVATE gui/${family}/ gui/)
            target_compile_definitions(${target} PRIVATE QT_NO_KEYWORDS)
            target_link_libraries(${target} LINK_PUBLIC gui_${family} ${GUI_LIBRARY_FILES_${ufamily}})
        endif()
        if (BUILD_PYTHON)
            target_link_libraries(${target} LINK_PUBLIC ${PYTHON_LIBRARIES})
        endif()
    endforeach (target)
endforeach (family)

file(GLOB_RECURSE CLANGFORMAT_FILES *.cc *.h)
string(REGEX REPLACE "[^;]*/ice40/chipdbs/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/ecp5/chipdbs/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/3rdparty[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/generated[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")

add_custom_target(
    clangformat
    COMMAND clang-format
    -style=file
    -i
    ${CLANGFORMAT_FILES}
)
