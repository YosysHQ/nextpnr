include(TestBigEndian)

test_big_endian(IS_BIG_ENDIAN)
if (IS_BIG_ENDIAN)
    set(BBASM_ENDIAN_FLAG "--be")
else()
    set(BBASM_ENDIAN_FLAG "--le")
endif()

# Example usage:
#
#   add_bba_compile_command(
#       TARGET chipdb-ice40
#       OUTPUT ice40/chipdb-hx8k.bin
#       INPUT  ice40/chipdb-hx8k.bba
#       MODE   binary
#   )
#
# All paths are relative to ${CMAKE_BINARY_DIR} (sic!).
#
function(add_bba_compile_command)
    cmake_parse_arguments(arg "" "DEPENDS;TARGET;OUTPUT;INPUT;MODE" "" ${ARGN})

    cmake_path(ABSOLUTE_PATH arg_INPUT BASE_DIRECTORY ${CMAKE_BINARY_DIR})

    if (NOT arg_DEPENDS)
        set(arg_DEPENDS ${arg_INPUT})
    endif()

    if (arg_MODE STREQUAL "binary" OR arg_MODE STREQUAL "resource")

        add_custom_command(
            OUTPUT
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG}
                ${arg_INPUT}
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.new
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.new
                    ${CMAKE_BINARY_DIR}/${arg_OUTPUT}
            DEPENDS
                bbasm
                ${arg_DEPENDS}
            VERBATIM
        )

        if (arg_MODE STREQUAL "resource")

            file(WRITE ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.rc
                "${arg_OUTPUT} RCDATA \"${CMAKE_BINARY_DIR}/${arg_OUTPUT}\"")

            target_sources(
                ${arg_TARGET} PRIVATE
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.rc
            )

        else()

            target_sources(
                ${arg_TARGET} PRIVATE
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}
            )

        endif()

    elseif (arg_MODE STREQUAL "embed")

        add_custom_command(
            OUTPUT
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG} --e
                ${arg_INPUT}
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc.new
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.new
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc.new
                    ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.new
                    ${CMAKE_BINARY_DIR}/${arg_OUTPUT}
            DEPENDS
                bbasm
                ${arg_DEPENDS}
            VERBATIM
        )

        target_sources(
            ${arg_TARGET} PRIVATE
            ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc
        )

    elseif (arg_MODE STREQUAL "string")

        add_custom_command(
            OUTPUT
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc
            COMMAND
                bbasm ${BBASM_ENDIAN_FLAG} --c
                ${arg_INPUT}
                ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc.new
            COMMAND
                ${CMAKE_COMMAND} -E rename # atomic update
                    ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc.new
                    ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc
            DEPENDS
                bbasm
                ${arg_DEPENDS}
            VERBATIM
        )

        target_sources(
            ${arg_TARGET} PRIVATE
            ${CMAKE_BINARY_DIR}/${arg_OUTPUT}.cc
        )

    endif()

endfunction()
