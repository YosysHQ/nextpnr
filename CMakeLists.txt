cmake_minimum_required(VERSION 3.13)
project(nextpnr CXX C)

include(CheckCXXCompilerFlag)

# Allow family.cmake add additional dependencies to gui_${family}.
cmake_policy(SET CMP0079 NEW)

# We want to explictly include all include directories when generating the
# compilation database as not all clang/gcc share the same implicit includes
# leading to essentially non-working compile_commands.json
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES 
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

# Enable IPO support.
cmake_policy(SET CMP0069 NEW)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported)

option(BUILD_GUI "Build GUI" OFF)
option(BUILD_PYTHON "Build Python Integration" ON)
option(BUILD_TESTS "Build tests" OFF)
option(USE_OPENMP "Use OpenMP to accelerate analytic placer" OFF)
option(COVERAGE "Add code coverage info" OFF)
option(STATIC_BUILD "Create static build" OFF)
option(EXTERNAL_CHIPDB "Create build with pre-built chipdb binaries" OFF)
option(WERROR "pass -Werror to compiler (used for CI)" OFF)
option(PROFILER "Link against libprofiler" OFF)
option(USE_IPO "Compile nextpnr with IPO" ON)
option(USE_RUST "Enable Rust bindings" OFF)

if (USE_RUST)
    add_subdirectory(3rdparty/corrosion)
    corrosion_import_crate(MANIFEST_PATH rust/Cargo.toml PROFILE "release" IMPORTED_CRATES RUST_CRATES)
endif()

if (USE_IPO)
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

if(WIN32 OR EXTERNAL_CHIPDB)
    set(BBASM_MODE "binary")
else()
    set(BBASM_MODE "string")
endif()

set(Boost_NO_BOOST_CMAKE ON)

find_package(Threads)
if (Threads_FOUND)
    find_package(TBB QUIET)
    if (TBB_FOUND)
        add_definitions(-DNEXTPNR_USE_TBB)
    endif()
else()
    add_definitions(-DNPNR_DISABLE_THREADS)
endif()

if(WASI)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lwasi-emulated-mman")
    add_definitions(
        -DBOOST_EXCEPTION_DISABLE
        -DBOOST_NO_EXCEPTIONS
    )
    if (NOT Threads_FOUND)
        add_definitions(-DBOOST_NO_CXX11_HDR_MUTEX)
    endif()
endif()

set(link_param "")
if (STATIC_BUILD)
    set(Boost_USE_STATIC_LIBS   ON)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        if (MSVC)
            set(CMAKE_CXX_FLAGS_RELEASE "/MT")
            set(CMAKE_CXX_FLAGS_DEBUG "/MTd")
        endif()
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so")
        set(link_param "-static")
        if (BUILD_PYTHON)
            find_package(ZLIB)
            find_package(EXPAT)
            find_package(Threads)
        endif()
    endif()
endif()

if (EXTERNAL_CHIPDB)
    set(EXTERNAL_CHIPDB_ROOT "${CMAKE_INSTALL_PREFIX}/share/nextpnr" CACHE STRING
        "External chipdb path")
    message(STATUS "Using external chipdb path: ${EXTERNAL_CHIPDB_ROOT}")
    add_definitions("-DEXTERNAL_CHIPDB_ROOT=\"${EXTERNAL_CHIPDB_ROOT}\"")
endif()

set(PROGRAM_PREFIX "" CACHE STRING "Name prefix for executables")

# List of families to build
set(FAMILIES generic ice40 ecp5 nexus gowin fpga_interchange machxo2 mistral himbaechel)
set(STABLE_FAMILIES generic ice40 ecp5)
set(EXPERIMENTAL_FAMILIES nexus gowin fpga_interchange machxo2 mistral himbaechel)

set(ARCH "" CACHE STRING "Architecture family for nextpnr build")
set_property(CACHE ARCH PROPERTY STRINGS ${FAMILIES})

if (NOT ARCH)
    message(STATUS "Architecture needs to be set, set desired one with -DARCH=xxx")
    message(STATUS "Supported architectures are :")
    message(STATUS "  all")
    message(STATUS "  all+alpha")
    foreach(item ${FAMILIES})
        message(STATUS "  ${item}")
    endforeach()
    message(FATAL_ERROR "Architecture setting is mandatory")
endif ()

if (ARCH STREQUAL "all+alpha")
    SET(ARCH ${STABLE_FAMILIES} ${EXPERIMENTAL_FAMILIES})
endif()


if (ARCH STREQUAL "all")
    SET(ARCH ${STABLE_FAMILIES})
endif()

foreach(item ${ARCH})
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
    foreach(TRY_WARN_FLAG no-unused-parameter no-missing-field-initializers no-array-bounds no-format-truncation)
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

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/3rdparty/sanitizers-cmake/cmake;." ${CMAKE_MODULE_PATH})

if (COVERAGE)
    include(CodeCoverage)
endif()

if(NOT DEFINED CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
     set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "No dev warnings")
endif()

find_package(Sanitizers)

# List of Boost libraries to include
set(boost_libs filesystem program_options iostreams system)
if (Threads_FOUND)
    list(APPEND boost_libs thread)
endif()

if (BUILD_GUI AND NOT BUILD_PYTHON)
    message(FATAL_ERROR "GUI requires Python to build")
endif()

if (BUILD_GUI)
    # For higher quality backtraces
    set(CMAKE_ENABLE_EXPORTS ON)
endif()

find_package(Python3 3.5 REQUIRED COMPONENTS Interpreter)
if (BUILD_PYTHON)
    # TODO: sensible minimum Python version
    find_package(Python3 3.5 REQUIRED COMPONENTS Development)
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

if (NOT DEFINED CURRENT_GIT_VERSION)
    # Get the latest abbreviated commit hash of the working branch
    # if not already defined outside (e.g. by package manager when building
    # outside of git repository)
    execute_process(
      COMMAND git describe --tags --always
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE CURRENT_GIT_VERSION
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

if (BUILD_TESTS)
    add_subdirectory(3rdparty/googletest/googletest ${CMAKE_CURRENT_BINARY_DIR}/generated/3rdparty/googletest EXCLUDE_FROM_ALL)
    enable_testing()
endif()

if (BUILD_GUI)
    add_subdirectory(3rdparty/QtPropertyBrowser ${CMAKE_CURRENT_BINARY_DIR}/generated/3rdparty/QtPropertyBrowser EXCLUDE_FROM_ALL)
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/common/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
)

if (NOT DEFINED PYBIND11_INCLUDE_DIR)
    # Use bundled pybind11
    set(PYBIND11_INCLUDE_DIR "3rdparty/pybind11/include")
endif()

include_directories(common/kernel/ common/place/ common/route/ json/ frontend/ 3rdparty/json11/  3rdparty/oourafft ${PYBIND11_INCLUDE_DIR} ${Boost_INCLUDE_DIRS} ${Python3_INCLUDE_DIRS})

find_package (Eigen3 REQUIRED NO_MODULE)
link_libraries(Eigen3::Eigen)

aux_source_directory(common/kernel/ KERNEL_SRC_FILES)
aux_source_directory(common/place/ PLACE_SRC_FILES)
aux_source_directory(common/route/ ROUTE_SRC_FILES)

aux_source_directory(json/ JSON_PARSER_FILES)
aux_source_directory(3rdparty/json11 EXT_JSON11_FILES)
aux_source_directory(3rdparty/oourafft EXT_OOURAFFT_FILES)
aux_source_directory(frontend/ FRONTEND_FILES)

aux_source_directory(rust/ RUST_FILES)

set(COMMON_FILES ${KERNEL_SRC_FILES} ${PLACE_SRC_FILES} ${ROUTE_SRC_FILES} ${EXT_JSON11_FILES} ${EXT_OOURAFFT_FILES} ${JSON_PARSER_FILES} ${FRONTEND_FILES})

if( NOT CMAKE_BUILD_TYPE )
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CROSSCOMPILING)
    set(BBA_IMPORT "IMPORTFILE-NOTFOUND" CACHE FILEPATH
        "Path to the `bba-export.cmake` export file from a native build")
    include(${BBA_IMPORT})
else()
    add_subdirectory(bba)
endif()

include(TestBigEndian)
test_big_endian(IS_BIG_ENDIAN)
if(IS_BIG_ENDIAN)
    set(BBASM_ENDIAN_FLAG "--be")
else()
    set(BBASM_ENDIAN_FLAG "--le")
endif()

set(EXTRA_LIB_DEPS)

if(PROFILER)
    list(APPEND EXTRA_LIB_DEPS profiler)
endif()
if(TBB_FOUND)
    list(APPEND EXTRA_LIB_DEPS TBB::tbb)
endif()

foreach (family ${ARCH})
    message(STATUS "Configuring architecture: ${family}")
    string(TOUPPER ${family} ufamily)
    aux_source_directory(${family}/ ${ufamily}_FILES)

    if (BUILD_GUI)
       add_subdirectory(gui ${CMAKE_CURRENT_BINARY_DIR}/generated/gui/${family} EXCLUDE_FROM_ALL)
    endif()

    # Add the CLI binary target
    add_executable(${PROGRAM_PREFIX}nextpnr-${family} ${COMMON_FILES} ${${ufamily}_FILES})
    if (WASI)
        # set(CMAKE_EXECUTABLE_SUFFIX) breaks CMake tests for some reason
        set_property(TARGET ${PROGRAM_PREFIX}nextpnr-${family} PROPERTY SUFFIX ".wasm")
    endif()
    install(TARGETS ${PROGRAM_PREFIX}nextpnr-${family} RUNTIME DESTINATION bin)
    target_compile_definitions(${PROGRAM_PREFIX}nextpnr-${family} PRIVATE MAIN_EXECUTABLE)

    # Add any new per-architecture targets here
    if (BUILD_TESTS)
        if (COVERAGE)
            APPEND_COVERAGE_COMPILER_FLAGS()
            set(COVERAGE_LCOV_EXCLUDES '/usr/include/*' '3rdparty/*' 'generated/*' 'bba/*' 'tests/*')
            SETUP_TARGET_FOR_COVERAGE_LCOV(
                NAME ${family}-coverage
                EXECUTABLE ${PROGRAM_PREFIX}nextpnr-${family}-test
                DEPENDENCIES ${PROGRAM_PREFIX}nextpnr-${family}-test
            )
        endif()

        aux_source_directory(tests/${family}/ ${ufamily}_TEST_FILES)
        if (BUILD_GUI)
            aux_source_directory(tests/gui/ GUI_TEST_FILES)
        endif()

        add_executable(${PROGRAM_PREFIX}nextpnr-${family}-test ${${ufamily}_TEST_FILES}
                ${COMMON_FILES} ${${ufamily}_FILES} ${GUI_TEST_FILES})
        target_link_libraries(${PROGRAM_PREFIX}nextpnr-${family}-test PRIVATE gtest_main)
        add_sanitizers(${PROGRAM_PREFIX}nextpnr-${family}-test)

        add_test(${family}-test ${CMAKE_CURRENT_BINARY_DIR}/nextpnr-${family}-test)
    endif()

    # Set ${family_targets} to the list of targets being build for this family
    set(family_targets ${PROGRAM_PREFIX}nextpnr-${family})

    if (BUILD_TESTS)
        set(family_targets ${family_targets} ${PROGRAM_PREFIX}nextpnr-${family}-test)
    endif()

    # Include the family-specific CMakeFile
    include(${family}/family.cmake)
    foreach (target ${family_targets})
        foreach(lib_dep ${EXTRA_LIB_DEPS})
            target_link_libraries(${target} PRIVATE ${lib_dep})
        endforeach()

        # Include family-specific source files to all family targets and set defines appropriately
        target_include_directories(${target} PRIVATE ${family}/ ${CMAKE_CURRENT_BINARY_DIR}/generated/ rust/)
        target_compile_definitions(${target} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family} ARCH_${ufamily} ARCHNAME=${family})
        target_link_libraries(${target} LINK_PUBLIC ${Boost_LIBRARIES} ${link_param})
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
            target_link_libraries(${target} LINK_PUBLIC ${Python3_LIBRARIES})
            if (STATIC_BUILD)
                target_link_libraries(${target} LINK_PUBLIC ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS} ${ZLIB_LIBRARIES} ${EXPAT_LIBRARIES})
                if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
                elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
                else()
                    target_link_libraries(${target} LINK_PUBLIC -lutil)
                endif()
            endif()
        endif()
        if (USE_RUST)
            target_sources(${target} PRIVATE ${RUST_FILES})
            target_compile_definitions(${target} PRIVATE USE_RUST)
            foreach(crate ${RUST_CRATES})
                target_link_libraries(${target} PRIVATE ${crate})
            endforeach(crate)
        endif()
    endforeach (target)
endforeach (family)

file(GLOB_RECURSE CLANGFORMAT_FILES *.cc *.h)
string(REGEX REPLACE "[^;]*/ice40/chipdb/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/ecp5/chipdb/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*nexus/chipdb/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/machxo2/chipdb/chipdb-[^;]*.cc" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/3rdparty[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/generated[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")
string(REGEX REPLACE "[^;]*/libmistral/[^;]*" "" CLANGFORMAT_FILES "${CLANGFORMAT_FILES}")

add_custom_target(
    clangformat
    COMMAND clang-format
    -style=file
    -i
    ${CLANGFORMAT_FILES}
)
