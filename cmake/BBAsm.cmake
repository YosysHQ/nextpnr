include(TestBigEndian)

test_big_endian(IS_BIG_ENDIAN)
if (IS_BIG_ENDIAN)
    set(BBASM_ENDIAN_FLAG "--be")
else()
    set(BBASM_ENDIAN_FLAG "--le")
endif()

# Example usage (note the `.new`, used for atomic updates):
#
#   add_bba_produce_command(
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
    cmake_parse_arguments(arg "" "OUTPUT" "COMMAND;INPUTS" ${ARGN})

    cmake_path(GET arg_OUTPUT PARENT_PATH arg_OUTPUT_DIR)
    file(MAKE_DIRECTORY ${arg_OUTPUT_DIR})

    list(GET arg_COMMAND 0 arg_EXECUTABLE)

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
            $ENV{SERIALIZE_BBA_PRODUCE_COMMAND}
        VERBATIM
    )

    if (BBASM_SERIALIZE)
        set(ENV{SERIALIZE_BBA_PRODUCE_COMMAND} ${arg_OUTPUT})
    endif()

endfunction()

# Example usage:
#
#   add_bba_compile_command(
#       TARGET chipdb-ice40
#       OUTPUT ${CMAKE_BINARY_DIR}/chipdb/ice40/chipdb-1k.bin
#       INPUT  ${CMAKE_CURRENT_BINARY_DIR}/chipdb-1k.bba
#       IDENT  ice40/chipdb-1k.bba
#       MODE   binary
#   )
#
# Paths must be absolute.
#
function(add_bba_compile_command)
    cmake_parse_arguments(arg "" "TARGET;OUTPUT;INPUT;IDENT;MODE" "" ${ARGN})

    cmake_path(GET arg_OUTPUT PARENT_PATH arg_OUTPUT_DIR)
    file(MAKE_DIRECTORY ${arg_OUTPUT_DIR})

    if (arg_MODE STREQUAL "binary" OR arg_MODE STREQUAL "resource")

        add_custom_command(
            OUTPUT
                ${arg_OUTPUT}
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG}
                ${arg_INPUT}
                ${arg_OUTPUT}.new
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${arg_OUTPUT}.new
                    ${arg_OUTPUT}
            DEPENDS
                bbasm
                ${arg_INPUT}
            VERBATIM
        )

        if (arg_MODE STREQUAL "resource")

            file(WRITE ${arg_OUTPUT}.rc
                "${arg_IDENT} RCDATA \"${arg_OUTPUT}\"")

            target_sources(
                ${arg_TARGET} PRIVATE
                ${arg_OUTPUT}.rc
            )

        else()

            target_sources(
                ${arg_TARGET} PRIVATE
                ${arg_OUTPUT}
            )

        endif()

    elseif (arg_MODE STREQUAL "embed")

        add_custom_command(
            OUTPUT
                ${arg_OUTPUT}.cc
                ${arg_OUTPUT}
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG} --e
                ${arg_INPUT}
                ${arg_OUTPUT}.cc.new
                ${arg_OUTPUT}.new
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${arg_OUTPUT}.cc.new
                    ${arg_OUTPUT}.cc
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${arg_OUTPUT}.new
                    ${arg_OUTPUT}
            DEPENDS
                bbasm
                ${arg_INPUT}
            VERBATIM
        )

        target_sources(
            ${arg_TARGET} PRIVATE
            ${arg_OUTPUT}.cc
        )

    elseif (arg_MODE STREQUAL "string")

        add_custom_command(
            OUTPUT
                ${arg_OUTPUT}.cc
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG} --c
                ${arg_INPUT}
                ${arg_OUTPUT}.cc.new
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${arg_OUTPUT}.cc.new
                    ${arg_OUTPUT}.cc
            DEPENDS
                bbasm
                ${arg_INPUT}
            VERBATIM
        )

        target_sources(
            ${arg_TARGET} PRIVATE
            ${arg_OUTPUT}.cc
        )

    endif()

endfunction()
