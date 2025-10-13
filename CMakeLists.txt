if (CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "In-tree builds are not supported. Instead, run:\ncmake . -B build <options> && cmake --build build")
endif()

cmake_minimum_required(VERSION 3.25)
project(nextpnr CXX)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

include(CheckCXXCompilerFlag)
include(CheckCXXCompilerHashEmbed)

if (NOT DEFINED CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "No dev warnings")
endif()

# We want to explictly include all include directories when generating the
# compilation database as not all clang/gcc share the same implicit includes
# leading to essentially non-working compile_commands.json
if (CMAKE_EXPORT_COMPILE_COMMANDS)
    set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
        ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

option(BUILD_GUI "Build GUI" OFF)
option(BUILD_PYTHON "Build Python integration" ON)
option(BUILD_RUST "Build Rust integration" OFF)
option(BUILD_TESTS "Build tests" OFF)
option(USE_OPENMP "Use OpenMP to accelerate analytic placer" OFF)
option(STATIC_BUILD "Create static build" OFF)
option(EXTERNAL_CHIPDB "Create build with pre-built chipdb binaries" OFF)
option(WERROR "pass -Werror to compiler (used for CI)" OFF)
option(PROFILER "Link against libprofiler" OFF)
option(USE_IPO "Compile nextpnr with IPO" ON)

set(PROGRAM_PREFIX "" CACHE STRING "Name prefix for executables")

if (BUILD_GUI AND NOT BUILD_PYTHON)
    message(FATAL_ERROR "GUI requires Python to build")
endif()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (USE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported)
    if (ipo_supported)
        message(STATUS "Building with IPO")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO is not supported with this compiler")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
    endif()
else()
    message(STATUS "Building without IPO")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
endif()

check_cxx_compiler_hash_embed(HAS_HASH_EMBED CXX_FLAGS_HASH_EMBED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_HASH_EMBED}")
if (EXTERNAL_CHIPDB)
    set(BBASM_MODE "binary")
elseif (HAS_HASH_EMBED)
    set(BBASM_MODE "embed")
elseif (WIN32 AND NOT HAS_HASH_EMBED)
    set(BBASM_MODE "resource")
    add_definitions(-DBBAS_ARE_RESOURCES)
else()
    set(BBASM_MODE "string")
endif()

find_package(Threads)
if (NOT Threads_FOUND)
    add_definitions(-DNPNR_DISABLE_THREADS)
endif()

if (WASI)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lwasi-emulated-mman")
    add_definitions(
        -DBOOST_EXCEPTION_DISABLE
        -DBOOST_NO_EXCEPTIONS
    )
    if (NOT Threads_FOUND)
        add_definitions(-DBOOST_NO_CXX11_HDR_MUTEX)
    endif()
endif()

if (STATIC_BUILD)
    set(Boost_USE_STATIC_LIBS   ON)
    set(Python_USE_STATIC_LIBS  ON)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        if (MSVC)
            set(CMAKE_CXX_FLAGS_RELEASE "/MT")
            set(CMAKE_CXX_FLAGS_DEBUG "/MTd")
        endif()
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        if (BUILD_PYTHON)
            find_package(ZLIB)
            find_package(EXPAT)
        endif()
    endif()
endif()

if (EXTERNAL_CHIPDB)
    set(EXTERNAL_CHIPDB_ROOT "${CMAKE_INSTALL_PREFIX}/share/nextpnr" CACHE STRING
        "External chipdb path")
    message(STATUS "Using external chipdb path: ${EXTERNAL_CHIPDB_ROOT}")
    add_definitions("-DEXTERNAL_CHIPDB_ROOT=\"${EXTERNAL_CHIPDB_ROOT}\"")
endif()

# List of families to build
set(FAMILIES generic ice40 ecp5 nexus machxo2 mistral himbaechel)
set(STABLE_FAMILIES generic ice40 ecp5)
set(EXPERIMENTAL_FAMILIES nexus machxo2 mistral himbaechel)

set(ARCH "" CACHE STRING "Architecture family for nextpnr build")
set_property(CACHE ARCH PROPERTY STRINGS ${FAMILIES})

if (NOT ARCH)
    message(STATUS "Architecture needs to be set, set desired one with -DARCH=xxx")
    message(STATUS "Supported architectures are :")
    message(STATUS "  all")
    message(STATUS "  all+alpha")
    foreach (item ${FAMILIES})
        message(STATUS "  ${item}")
    endforeach()
    message(FATAL_ERROR "Architecture setting is mandatory")
endif()

if (ARCH STREQUAL "all+alpha")
    set(ARCH ${STABLE_FAMILIES} ${EXPERIMENTAL_FAMILIES})
elseif (ARCH STREQUAL "all")
    set(ARCH ${STABLE_FAMILIES})
endif()

foreach (item ${ARCH})
    if (NOT item IN_LIST FAMILIES)
        message(FATAL_ERROR "Architecture '${item}' not in list of supported architectures")
    endif()
endforeach()

set(CMAKE_CXX_STANDARD 17)
if (MSVC)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /D_DEBUG /W4 /wd4100 /wd4244 /wd4125 /wd4800 /wd4456 /wd4458 /wd4305 /wd4459 /wd4121 /wd4996")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /W4 /wd4100 /wd4244 /wd4125 /wd4800 /wd4456 /wd4458 /wd4305 /wd4459 /wd4121 /wd4996 /wd4127")
else()
    # N.B. the -Wno-array-bounds is to work around a false positive in GCC 9
    set(WARN_FLAGS "-Wall -Wextra")
    foreach (TRY_WARN_FLAG no-unused-parameter no-missing-field-initializers no-array-bounds no-format-truncation)
        check_cxx_compiler_flag("-W${TRY_WARN_FLAG}" HAS_W${TRY_WARN_FLAG})
        if (HAS_W${TRY_WARN_FLAG})
            set(WARN_FLAGS "${WARN_FLAGS} -W${TRY_WARN_FLAG}")
        endif()
    endforeach()
    if (WERROR)
        set(WARN_FLAGS "${WARN_FLAGS} -Werror")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "${WARN_FLAGS} -fPIC -ggdb -pipe")
    if (USE_OPENMP)
        set(CMAKE_CXX_FLAGS_RELEASE "${WARN_FLAGS} -fPIC -O3 -g -pipe -fopenmp")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${WARN_FLAGS} -fPIC -O3 -g -pipe")
    endif()
endif()

set(boost_libs filesystem program_options iostreams system)
if (Threads_FOUND)
    list(APPEND boost_libs thread)
endif()
find_package(Boost REQUIRED COMPONENTS ${boost_libs})

if (BUILD_PYTHON)
    # TODO: sensible minimum Python version
    find_package(Python3 3.5 REQUIRED COMPONENTS Interpreter Development.Embed)
    find_package(pybind11 CONFIG)
    if (NOT pybind11_FOUND)
        message(STATUS "Using built-in pybind11")
        add_subdirectory(3rdparty/pybind11 EXCLUDE_FROM_ALL)
    endif()
    include_directories(${Python3_INCLUDE_DIRS})
else()
    find_package(Python3 3.5 REQUIRED COMPONENTS Interpreter)
    add_definitions("-DNO_PYTHON")
endif()

if (BUILD_RUST)
    add_subdirectory(3rdparty/corrosion)
    corrosion_import_crate(MANIFEST_PATH rust/Cargo.toml PROFILE "release" IMPORTED_CRATES RUST_CRATES)
else()
    add_definitions("-DNO_RUST")
endif()

if (BUILD_GUI)
    # Find the Qt6/5 libraries
    find_package(Qt6 COMPONENTS Core Widgets OpenGL OpenGLWidgets REQUIRED)
    if (Qt6_FOUND)
        message(STATUS "Using Qt6")
    else()
        message(STATUS "Using Qt5")
        find_package(Qt5 COMPONENTS Core Widgets OpenGL REQUIRED)
    endif()

    # For higher quality backtraces
    set(CMAKE_ENABLE_EXPORTS ON)

    add_subdirectory(3rdparty/QtPropertyBrowser EXCLUDE_FROM_ALL)
else()
    add_definitions(-DNO_GUI)
endif()

find_package(Eigen3 REQUIRED NO_MODULE)

add_subdirectory(3rdparty/json11)

add_subdirectory(3rdparty/oourafft)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/3rdparty/sanitizers-cmake/cmake" ${CMAKE_MODULE_PATH})
find_package(Sanitizers)

if (BUILD_TESTS)
    add_subdirectory(3rdparty/googletest/googletest EXCLUDE_FROM_ALL)
    enable_testing()
endif()

if (CMAKE_CROSSCOMPILING)
    set(BBA_IMPORT "IMPORTFILE-NOTFOUND" CACHE FILEPATH
        "Path to the `bba-export.cmake` export file from a native build")
    include(${BBA_IMPORT})
else()
    add_subdirectory(bba)
endif()

include(BBAsm)

add_subdirectory(common)
add_subdirectory(common/kernel)
add_subdirectory(common/place)
add_subdirectory(common/route)
add_subdirectory(frontend)
add_subdirectory(json)
add_subdirectory(rust)

if (BUILD_TESTS)
    add_subdirectory(tests/gui)
endif()

add_custom_target(nextpnr-all-bba)

function(add_nextpnr_architecture target)
    cmake_parse_arguments(arg "" "MAIN_SOURCE" "CORE_SOURCES;TEST_SOURCES;CURRENT_SOURCE_DIR;CURRENT_BINARY_DIR" ${ARGN})

    if (NOT arg_CURRENT_SOURCE_DIR)
        set(arg_CURRENT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    if (NOT arg_CURRENT_BINARY_DIR)
        set(arg_CURRENT_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
    endif()

    set(arg_MAIN_SOURCE "${arg_CURRENT_SOURCE_DIR}/${arg_MAIN_SOURCE}")
    list(TRANSFORM arg_CORE_SOURCES PREPEND ${arg_CURRENT_SOURCE_DIR}/)
    list(TRANSFORM arg_TEST_SOURCES PREPEND ${arg_CURRENT_SOURCE_DIR}/)

    # Defs library: used by everything
    #
    # This library doesn't include any code, and is used to share compiler options.

    add_library(nextpnr-${target}-defs INTERFACE)

    target_include_directories(nextpnr-${target}-defs INTERFACE
        ${CMAKE_SOURCE_DIR}/common/kernel
        ${arg_CURRENT_SOURCE_DIR}
    )

    string(TOUPPER ${family} family_upper)
    target_compile_definitions(nextpnr-${target}-defs INTERFACE
        NEXTPNR_NAMESPACE=nextpnr_${family}
        ARCHNAME=${family}
        ARCH_${family_upper}
    )

    # Core library: used by both CLI/GUI and tests

    add_library(nextpnr-${target}-core INTERFACE)

    target_sources(nextpnr-${target}-core INTERFACE
        ${arg_CORE_SOURCES}
    )

    target_link_libraries(nextpnr-${target}-core INTERFACE
        nextpnr-${target}-defs
        nextpnr_kernel
        nextpnr_place
        nextpnr_route
        nextpnr_frontend
        nextpnr_json
    )

    target_link_libraries(nextpnr-${target}-core INTERFACE
        Boost::headers
        ${Boost_LIBRARIES}
        Eigen3::Eigen
        oourafft
    )

    if (Threads_FOUND)
        target_link_libraries(nextpnr-${target}-core INTERFACE Threads::Threads)
    endif()

    if (BUILD_GUI)
        add_subdirectory(${CMAKE_SOURCE_DIR}/gui ${arg_CURRENT_BINARY_DIR}/gui)

        # Upsettingly, there is a cyclic dependency between `common/kernel` and `gui`, so these
        # two libraries have to be added separately to all executable targets.
    endif()

    if (BUILD_PYTHON)
        target_link_libraries(nextpnr-${target}-core INTERFACE ${Python3_LIBRARIES})
        if (STATIC_BUILD)
            target_link_libraries(nextpnr-${target}-core INTERFACE ZLIB::ZLIB EXPAT::EXPAT)
        endif()
    endif()

    if (BUILD_RUST)
        foreach (crate ${RUST_CRATES})
            target_link_libraries(nextpnr-${target}-core INTERFACE ${crate})
        endforeach()
    endif()

    if (PROFILER)
        target_link_libraries(nextpnr-${target}-core INTERFACE profiler)
    endif()

    add_sanitizers(nextpnr-${target}-core)

    # Chip database

    add_library(nextpnr-${target}-bba INTERFACE EXCLUDE_FROM_ALL)

    add_dependencies(nextpnr-all-bba nextpnr-${target}-bba)

    add_library(nextpnr-${target}-chipdb INTERFACE EXCLUDE_FROM_ALL)

    target_link_libraries(nextpnr-${target}-core INTERFACE nextpnr-${target}-chipdb)

    # CLI/GUI runner

    add_executable(nextpnr-${target} ${arg_MAIN_SOURCE})
    set_property(TARGET nextpnr-${target} PROPERTY RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    set_property(TARGET nextpnr-${target} PROPERTY OUTPUT_NAME ${PROGRAM_PREFIX}nextpnr-${target})
    if (WASI)
        # set(CMAKE_EXECUTABLE_SUFFIX) breaks CMake tests for some reason
        set_property(TARGET nextpnr-${target} PROPERTY SUFFIX ".wasm")
    endif()

    target_link_libraries(nextpnr-${target} PRIVATE nextpnr-${target}-core)
    if (BUILD_GUI)
        target_link_libraries(nextpnr-${target} PRIVATE nextpnr-${target}-gui)
    endif()

    install(TARGETS nextpnr-${target} RUNTIME DESTINATION bin)

    # Test runner

    if (BUILD_TESTS)
        add_test(NAME nextpnr-${target}-test COMMAND nextpnr-${target}-test)

        add_executable(nextpnr-${target}-test ${arg_TEST_SOURCES})
        set_property(TARGET nextpnr-${target}-test PROPERTY RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

        target_include_directories(nextpnr-${target}-test PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/googletest/googletest/include)

        target_link_libraries(nextpnr-${target}-test PRIVATE gtest_main nextpnr-${target}-core)
        if (BUILD_GUI)
            target_link_libraries(nextpnr-${target}-test PRIVATE nextpnr-${target}-gui)
            target_link_libraries(nextpnr-${target}-test PRIVATE nextpnr_test_gui)
        endif()
    endif()

endfunction()

foreach (family ${ARCH})
    message(STATUS "Configuring architecture: ${family}")
    add_subdirectory(${family})
endforeach()

file(GLOB_RECURSE CLANGFORMAT_FILES *.cc *.h)
string(REGEX REPLACE "[^;]*/ice40/chipdb/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/ecp5/chipdb/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/nexus/chipdb/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/machxo2/chipdb/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/3rdparty/[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/libmistral/[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")

add_custom_target(clangformat
    COMMAND clang-format
        -style=file
        -i
        ${CLANGFORMAT_FILES}
)
