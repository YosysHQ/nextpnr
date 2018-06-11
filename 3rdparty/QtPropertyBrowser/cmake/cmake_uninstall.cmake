################ CMake Uninstall Template #######################
# CMake Template file for uninstallation of files
# mentioned in 'install_manifest.txt'
#
# Used by uinstall target
#################################################################

MESSAGE(STATUS "======================================================")
MESSAGE(STATUS "================  Uninstalling  ======================")

SET(MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/install_manifest.txt")
 
IF(NOT EXISTS ${MANIFEST})
    MESSAGE(FATAL_ERROR "Cannot find install manifest: '${MANIFEST}'")
ENDIF()
 
FILE(STRINGS ${MANIFEST} files)
FOREACH(file ${files})
    IF(EXISTS ${file})
        MESSAGE(STATUS "Removing file: '${file}'")
 
        EXECUTE_PROCESS(
            COMMAND ${CMAKE_COMMAND} -E remove ${file}
            OUTPUT_VARIABLE rm_out
            RESULT_VARIABLE rm_retval
        )
        
        IF(NOT "${rm_retval}" STREQUAL 0)
            MESSAGE(FATAL_ERROR "Failed to remove file: '${file}'.")
        ENDIF()
    ELSE()
        MESSAGE(STATUS "File '${file}' does not exist.")
    ENDIF()
ENDFOREACH(file)




