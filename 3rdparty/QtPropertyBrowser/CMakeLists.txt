CMAKE_MINIMUM_REQUIRED(VERSION 2.8.11)
PROJECT(QtPropertyBrowser)

########################### Misc. Configs ##############################
# Add custom cmake files folder
SET (PROJECT_CMAKE_DIR ${PROJECT_SOURCE_DIR}/cmake)
SET (CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${PROJECT_CMAKE_DIR}")

# Add custom Compile Settings and flags
INCLUDE(CompileSettings)

# Add custom Install Settings
INCLUDE(InstallSettings)

##################### Look for required libraries ######################

# Add QT dependencies
FIND_PACKAGE(Qt5Widgets REQUIRED)

######################### Add Primary Targets ##########################
ADD_SUBDIRECTORY(src)

########################## Add the Examples ############################
OPTION (BUILD_EXAMPLES "Build Examples" ON)
IF(BUILD_EXAMPLES)
  ADD_SUBDIRECTORY(examples)
ENDIF()

######################### Installation Stuff ###########################
INCLUDE(InstallProjectConfig)

###################### Add uninstall target ############################
ADD_CUSTOM_TARGET(uninstall
  COMMAND ${CMAKE_COMMAND} -P ${PROJECT_CMAKE_DIR}/cmake_uninstall.cmake)

##################### Show Configuration Summary ######################
MESSAGE(STATUS "===============================================================")
MESSAGE(STATUS "================  Configuration Summary  ======================")
MESSAGE(STATUS "Project Name:       ${PROJECT_NAME}")
MESSAGE(STATUS "Build type:         ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "Build type Flags:   ${CMAKE_BUILD_TYPE_FLAGS}")
MESSAGE(STATUS "C++ compile flags:  ${CMAKE_CXX_FLAGS}")
MESSAGE(STATUS "Install Path:       ${CMAKE_INSTALL_PREFIX}")
MESSAGE(STATUS "Build Examples:     ${BUILD_EXAMPLES}")
IF(Qt5Widgets_FOUND)
  MESSAGE(STATUS "QT Version:         ${Qt5Widgets_VERSION_STRING}")
  MESSAGE(STATUS "QT INCLUDE DIRS:    ${Qt5Widgets_INCLUDE_DIRS}")
  MESSAGE(STATUS "QT COMPILE DEFS:    ${Qt5Widgets_COMPILE_DEFINITIONS}")
  MESSAGE(STATUS "QT DEFS:            ${Qt5Widgets_DEFINITIONS}")
  MESSAGE(STATUS "QT EXE FLAGS:       ${Qt5Widgets_EXECUTABLE_COMPILE_FLAGS}")
ENDIF()
