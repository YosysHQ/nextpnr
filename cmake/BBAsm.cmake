include(TestBigEndian)

test_big_endian(IS_BIG_ENDIAN)
if (IS_BIG_ENDIAN)
    set(BBASM_ENDIAN_FLAG "--be")
else()
    set(BBASM_ENDIAN_FLAG "--le")
endif()

set(EXPORT_BBA_FILES "" CACHE PATH "Copy generated .bba files to this directory")
set(IMPORT_BBA_FILES "" CACHE PATH "Use pre-generated .bba files from this directory")
mark_as_advanced(EXPORT_BBA_FILES IMPORT_BBA_FILES)

if (EXPORT_BBA_FILES)
    file(REAL_PATH ${EXPORT_BBA_FILES} EXPORT_BBA_FILES BASE_DIRECTORY ${CMAKE_BINARY_DIR})
    message(STATUS "Exporting .bba files to ${EXPORT_BBA_FILES}")
endif()

if (IMPORT_BBA_FILES)
    file(REAL_PATH ${IMPORT_BBA_FILES} IMPORT_BBA_FILES BASE_DIRECTORY ${CMAKE_BINARY_DIR})
    message(STATUS "Importing .bba files from ${EXPORT_BBA_FILES}")
endif()

# Example usage (note the `.new`, used for atomic updates):
#
#   add_bba_produce_command(
#       TARGET  nextpnr-ice40-bba
#       COMMAND ${Python_EXECUTABLE}
#           ${CMAKE_CURRENT_SOURCE_DIR}/chipdb.py
#           -o ${CMAKE_CURRENT_BINARY_DIR}/chipdb-hx8k.bba.new
#       OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/chipdb-hx8k.bba
#       INPUTS  ${CMAKE_CURRENT_SOURCE_DIR}/chipdb.py
#   )
#
# Paths must be absolute.
#
function(add_bba_produce_command)
    cmake_parse_arguments(arg "" "OUTPUT" "TARGET;COMMAND;INPUTS" ${ARGN})

    cmake_path(GET arg_OUTPUT PARENT_PATH arg_OUTPUT_DIR)
    cmake_path(GET arg_OUTPUT FILENAME arg_OUTPUT_NAME)
    file(RELATIVE_PATH arg_OUTPUT_RELATIVE ${CMAKE_BINARY_DIR} ${arg_OUTPUT})
    file(MAKE_DIRECTORY ${arg_OUTPUT_DIR})

    list(GET arg_COMMAND 0 arg_EXECUTABLE)

    if (NOT IMPORT_BBA_FILES)

        add_custom_command(
            OUTPUT
                ${arg_OUTPUT}
            COMMAND
                ${arg_COMMAND}
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${arg_OUTPUT}.new
                    ${arg_OUTPUT}
            DEPENDS
                ${arg_EXECUTABLE}
                ${arg_INPUTS}
            VERBATIM
        )

    else()

        add_custom_command(
            OUTPUT
                ${arg_OUTPUT}
            COMMAND
                ${CMAKE_COMMAND} -E copy
                    ${IMPORT_BBA_FILES}/${arg_OUTPUT_RELATIVE}
                    ${arg_OUTPUT}
            DEPENDS
                ${IMPORT_BBA_FILES}/${arg_OUTPUT_RELATIVE}
            COMMENT
                "Importing ${arg_OUTPUT_NAME}"
            VERBATIM
        )

    endif()

    if (EXPORT_BBA_FILES)

        add_custom_command(
            OUTPUT
                ${arg_OUTPUT}
            COMMAND
                ${CMAKE_COMMAND} -E copy
                    ${arg_OUTPUT}
                    ${EXPORT_BBA_FILES}/${arg_OUTPUT_RELATIVE}
            APPEND
            VERBATIM
        )

    endif()

    target_sources(
        ${arg_TARGET} PUBLIC
        ${arg_OUTPUT}
    )

endfunction()

# Example usage:
#
#   add_bba_compile_command(
#       TARGET  chipdb-ice40
#       OUTPUT  ice40/chipdb-1k.bin
#       INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-1k.bba
#       MODE    binary
#   )
#
# Paths must be absolute.
#
function(add_bba_compile_command)
    cmake_parse_arguments(arg "" "TARGET;OUTPUT;INPUT;MODE" "" ${ARGN})

    cmake_path(GET arg_OUTPUT PARENT_PATH arg_OUTPUT_DIR)
    cmake_path(GET arg_OUTPUT FILENAME arg_OUTPUT_NAME)

    set(arg_PRODUCT ${CMAKE_BINARY_DIR}/share/${arg_OUTPUT})
    cmake_path(GET arg_PRODUCT PARENT_PATH arg_PRODUCT_DIR)

    if (arg_MODE STREQUAL "binary" OR arg_MODE STREQUAL "resource")

        add_custom_command(
            OUTPUT
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG}
                ${arg_INPUT}
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
            DEPENDS
                bbasm
                ${arg_INPUT}
            VERBATIM
        )

        if (arg_MODE STREQUAL "resource")

            file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.rc
                "${arg_OUTPUT} RCDATA \"${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}\"")

            target_sources(
                ${arg_TARGET} PUBLIC
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.rc
            )

        else()

            target_sources(
                ${arg_TARGET} PUBLIC
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
            )

            add_custom_command(
                OUTPUT
                    ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
                COMMAND
                    ${CMAKE_COMMAND} -E make_directory
                    ${arg_PRODUCT_DIR}
                COMMAND
                    ${CMAKE_COMMAND} -E copy
                    ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
                    ${arg_PRODUCT}
                APPEND
                VERBATIM
            )

            install(
                FILES ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
                DESTINATION share/nextpnr/${arg_OUTPUT_DIR}
            )

        endif()

    elseif (arg_MODE STREQUAL "embed")

        add_custom_command(
            OUTPUT
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.cc
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG} --e
                ${arg_INPUT}
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.cc
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}
            DEPENDS
                bbasm
                ${arg_INPUT}
            VERBATIM
        )

        target_sources(
            ${arg_TARGET} PUBLIC
            ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.cc
        )

    elseif (arg_MODE STREQUAL "string")

        add_custom_command(
            OUTPUT
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.cc
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG} --c
                ${arg_INPUT}
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.cc
            DEPENDS
                bbasm
                ${arg_INPUT}
            VERBATIM
        )

        if (NOT MSVC)
            set_source_files_properties(
                ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.cc PROPERTIES
                COMPILE_OPTIONS "-w;-g0;-O0"
            )
        endif()

        target_sources(
            ${arg_TARGET} PUBLIC
            ${CMAKE_CURRENT_BINARY_DIR}/${arg_OUTPUT_NAME}.cc
        )

    endif()

endfunction()
