###################### Installation Settings ###########################

# Set a local folder for default installation when CMAKE_INSTALL_PREFIX is NOT specified
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  IF(WIN32)
  SET(DEFAULT_INSTALL_PATH "$ENV{HOMEDRIVE}/${PROJECT_NAME}" )
  ELSE()
  SET(DEFAULT_INSTALL_PATH "$ENV{HOME}/local")
  ENDIF()
  SET(CMAKE_INSTALL_PREFIX "${DEFAULT_INSTALL_PATH}" CACHE PATH "${PROJECT_NAME} install prefix" FORCE) 
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)

# Offer the user the choice of overriding the installation directories
SET(INSTALL_LIB_DIR lib CACHE PATH "Install dir for libraries")
SET(INSTALL_BIN_DIR bin CACHE PATH "Install dir for executables")
SET(INSTALL_INCLUDE_DIR include/${PROJECT_NAME} CACHE PATH "Install dir for headers")
IF(WIN32 AND NOT CYGWIN)
  SET(DEF_INSTALL_CMAKE_DIR CMake)
ELSE()
  SET(DEF_INSTALL_CMAKE_DIR lib/cmake/${PROJECT_NAME})
ENDIF()
SET(INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH "Install dir for CMake files")

# Make relative paths absolute (needed later on)
FOREACH(p LIB BIN INCLUDE CMAKE)
  SET(var INSTALL_${p}_DIR)
  IF(NOT IS_ABSOLUTE "${${var}}")
    SET(${var} "${CMAKE_INSTALL_PREFIX}/${${var}}")
  ENDIF()
ENDFOREACH()

