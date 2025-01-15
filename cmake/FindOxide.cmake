if (DEFINED ENV{OXIDE_INSTALL_PREFIX})
    set(oxide_default_install_prefix $ENV{OXIDE_INSTALL_PREFIX})
else()
    set(oxide_default_install_prefix ${CMAKE_INSTALL_PREFIX})
endif()
set(OXIDE_INSTALL_PREFIX "${oxide_default_install_prefix}" CACHE STRING
    "prjoxide install prefix")
message(STATUS "prjoxide install prefix: ${OXIDE_INSTALL_PREFIX}")

set(PRJOXIDE_TOOL ${OXIDE_INSTALL_PREFIX}/bin/prjoxide)
message(STATUS "prjoxide tool path: ${PRJOXIDE_TOOL}")

return(PROPAGATE PRJOXIDE_TOOL)
