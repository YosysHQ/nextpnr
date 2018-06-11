######################### COMPILE SETTINGS ################################
IF(NOT CMAKE_BUILD_TYPE)
   SET(CMAKE_BUILD_TYPE Release CACHE STRING
       "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
ENDIF(NOT CMAKE_BUILD_TYPE)

MESSAGE(STATUS "===============================================================")
MESSAGE(STATUS "============ Configuring CompileSettings  =====================")


IF(CMAKE_COMPILER_IS_GNUCC)

  SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
  SET (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native -funroll-loops -ffast-math")
  SET (CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -march=native -funroll-loops")

  OPTION (USE_PEDANTIC_FLAGS "Use Pedantic Flags in GCC" ON)
  IF(USE_PEDANTIC_FLAGS)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wno-long-long -Wno-variadic-macros")
    SET(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -pedantic -Wno-long-long -Wno-variadic-macros")
  ENDIF()
  
  OPTION (USE_DEBUG_SYMBOLS "Use Debug Symbols" OFF)
  IF(USE_DEBUG_SYMBOLS)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    SET(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -g")
  ENDIF()
  
ENDIF(CMAKE_COMPILER_IS_GNUCC)

IF(NOT MSVC)
  OPTION (USE_CPP_11 "Use C++11 Compiler" ON)
  IF(USE_CPP_11)
    INCLUDE(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
    CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
    
    IF(COMPILER_SUPPORTS_CXX11)
  	  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    ELSEIF(COMPILER_SUPPORTS_CXX0X)
  	  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
    ELSE()
      SET(USE_CPP_11 OFF)
      MESSAGE(STATUS "Compiler ${CMAKE_CXX_COMPILER} has no C++11 support.")
    ENDIF()
  ENDIF()
ENDIF()

IF(CMAKE_BUILD_TYPE MATCHES Debug)
  SET(CMAKE_BUILD_TYPE_FLAGS ${CMAKE_CXX_FLAGS_DEBUG})
ELSEIF(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  SET(CMAKE_BUILD_TYPE_FLAGS ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
ELSEIF(CMAKE_BUILD_TYPE MATCHES Release)
  SET(CMAKE_BUILD_TYPE_FLAGS ${CMAKE_CXX_FLAGS_RELEASE})
ENDIF()

OPTION (USE_OpenMP "Use OpenMP" ON)
IF(USE_OpenMP)
  FIND_PACKAGE(OpenMP)
  IF(OPENMP_FOUND)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
  ENDIF()
ENDIF()

MESSAGE(STATUS "===============================================================")