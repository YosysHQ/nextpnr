# TODO: sensible minimum CMake version
cmake_minimum_required(VERSION 3.3)
project(nextpnr)

set(FAMILIES dummy ice40)
set(CMAKE_CXX_STANDARD 11)
# set(CMAKE_CXX_FLAGS "-Wall -pedantic -Wextra -Werror")
set(CMAKE_DEFIN)
set(boost_libs filesystem thread)
# TODO: sensible minimum Python version
find_package(PythonInterp 3.5 REQUIRED)
find_package(PythonLibs 3.5 REQUIRED)

find_package(Boost REQUIRED COMPONENTS ${boost_libs})

# Find Boost::Python of a suitable version in a cross-platform way

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
endif ()

# TODO: Find and include Qt

include_directories(common/ ${Boost_INCLUDE_DIRS} ${PYTHON_INCLUDE_DIRS})
aux_source_directory(common/ COMMON_FILES)

foreach (family ${FAMILIES})
    string(TOUPPER ${family} ufamily)
    aux_source_directory(${family}/ ${ufamily}_FILES)
    add_executable(nextpnr-${family} ${COMMON_FILES} ${${ufamily}_FILES})
    # TODO: also build an importable Python module
    PYTHON_ADD_MODULE(nextpnrpy_${family} ${COMMON_FILES} ${${ufamily}_FILES})
    set(family_targets nextpnr-${family} nextpnrpy_${family})
    include(${family}/family.cmake)
    foreach (target ${family_targets})
        target_include_directories(${target} PRIVATE ${family}/)
        target_compile_definitions(${target} PRIVATE ARCH_${ufamily} ARCHNAME=${family})
        target_link_libraries(${target} LINK_PUBLIC ${Boost_LIBRARIES} ${PYTHON_LIBRARIES})
    endforeach (target)
endforeach (family)
