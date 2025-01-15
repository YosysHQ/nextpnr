set(TRELLIS_PROGRAM_PREFIX "" CACHE STRING
    "Trellis name prefix")
if (TRELLIS_PROGRAM_PREFIX)
    message(STATUS "Trellis program prefix: ${TRELLIS_PROGRAM_PREFIX}")
endif()

if (DEFINED ENV{TRELLIS_INSTALL_PREFIX})
    set(trellis_default_install_prefix $ENV{TRELLIS_INSTALL_PREFIX})
else()
    set(trellis_default_install_prefix ${CMAKE_INSTALL_PREFIX})
endif()

set(TRELLIS_INSTALL_PREFIX ${trellis_default_install_prefix} CACHE STRING
    "Trellis install prefix")
message(STATUS "Trellis install prefix: ${TRELLIS_INSTALL_PREFIX}")

if (NOT TRELLIS_LIBDIR)
    # The pytrellis library isn't a normal shared library, but rather a native Python library;
    # it does not follow the normal platform conventions for shared libraries, so we can't just
    # use find_library() here. Instead, we emulate the useful parts of the find_library() logic
    # for use with find_path().
    set(pytrellis_paths)
    foreach (prefix_path ${CMAKE_PREFIX_PATH})
        list(APPEND pytrellis_paths ${prefix_path}/lib)
        if (CMAKE_LIBRARY_ARCHITECTURE)
            list(APPEND pytrellis_paths ${prefix_path}/lib/${CMAKE_LIBRARY_ARCHITECTURE})
        endif()
    endforeach()
    list(APPEND pytrellis_paths ${CMAKE_LIBRARY_PATH})
    if (NOT NO_CMAKE_SYSTEM_PATH)
        foreach (prefix_path ${CMAKE_SYSTEM_PREFIX_PATH})
            list(APPEND pytrellis_paths ${prefix_path}/lib)
            if (CMAKE_LIBRARY_ARCHITECTURE)
                list(APPEND pytrellis_paths ${prefix_path}/lib/${CMAKE_LIBRARY_ARCHITECTURE})
            endif()
        endforeach()
        list(APPEND pytrellis_paths ${CMAKE_SYSTEM_LIBRARY_PATH})
    endif()
    message(STATUS "Searching for pytrellis in: ${pytrellis_paths}")

    if (WIN32)
        set(pytrellis_lib pytrellis.pyd)
    else()
        set(pytrellis_lib pytrellis${CMAKE_SHARED_MODULE_SUFFIX})
    endif()

    find_path(TRELLIS_LIBDIR ${pytrellis_lib}
        HINTS ${TRELLIS_INSTALL_PREFIX}/lib/${TRELLIS_PROGRAM_PREFIX}trellis
        PATHS ${pytrellis_paths}
        PATH_SUFFIXES ${TRELLIS_PROGRAM_PREFIX}trellis
        DOC "Location of the pytrellis library")
    if (NOT TRELLIS_LIBDIR)
        message(FATAL_ERROR "Failed to locate the pytrellis library")
    endif()
endif()
message(STATUS "Trellis library directory: ${TRELLIS_LIBDIR}")

if (NOT TRELLIS_DATADIR)
    set(TRELLIS_DATADIR ${TRELLIS_INSTALL_PREFIX}/share/${TRELLIS_PROGRAM_PREFIX}trellis)
endif()
message(STATUS "Trellis data directory: ${TRELLIS_DATADIR}")

return(PROPAGATE TRELLIS_LIBDIR TRELLIS_DATADIR)
