set(icestorm_default_install_prefix ${CMAKE_INSTALL_PREFIX})
if (DEFINED ENV{ICESTORM_INSTALL_PREFIX})
    set(icestorm_default_install_prefix $ENV{ICESTORM_INSTALL_PREFIX})
endif()
set(ICESTORM_INSTALL_PREFIX ${icestorm_default_install_prefix} CACHE STRING
    "IceStorm install prefix")
message(STATUS "IceStorm install prefix: ${ICESTORM_INSTALL_PREFIX}")

if (NOT ICEBOX_DATADIR)
    set(ICEBOX_DATADIR ${ICESTORM_INSTALL_PREFIX}/share/icebox)
endif()
message(STATUS "icebox data directory: ${ICEBOX_DATADIR}")

return(PROPAGATE ICEBOX_DATADIR)
