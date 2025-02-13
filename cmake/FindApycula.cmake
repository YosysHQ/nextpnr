# nextpnr-gowin only

find_program (GOWIN_BBA_EXECUTABLE gowin_bba)
message(STATUS "gowin_bba executable: ${GOWIN_BBA_EXECUTABLE}")

# nextpnr-himbaechel-gowin only

if (DEFINED ENV{APYCULA_INSTALL_PREFIX})
    set(apycula_default_install_prefix $ENV{APYCULA_INSTALL_PREFIX})
endif()
set(APYCULA_INSTALL_PREFIX ${apycula_default_install_prefix} CACHE STRING
    "Apycula install prefix (virtualenv directory)")
if (NOT APYCULA_INSTALL_PREFIX STREQUAL "")
	message(STATUS "Apycula install prefix: ${APYCULA_INSTALL_PREFIX}")
	set(apycula_Python3_EXECUTABLE ${APYCULA_INSTALL_PREFIX}/bin/python)
else()
	message(STATUS "Apycula install prefix: (not set, using Python: ${Python3_EXECUTABLE})")
	set(apycula_Python3_EXECUTABLE ${Python3_EXECUTABLE})
endif()
