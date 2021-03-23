function(add_board)
    # ~~~
    # add_board(
    #    name <board name>
    #    device <common device>
    #    package <package>
    # )
    # ~~~
    #
    # Generates a board target containing information on the common device and package
    # of the board.
    #
    # Arguments:
    #   - name: name of the board. E.g. arty
    #   - device: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix
    #   - package: one of the packages available for a given device. E.g. cpg236
    #
    # Targets generated:
    #   - board-<name>

    set(options)
    set(oneValueArgs name device package)
    set(multiValueArgs)

    cmake_parse_arguments(
        add_board
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(name ${add_board_name})
    set(device ${add_board_device})
    set(package ${add_board_package})

    add_custom_target(board-${name} DEPENDS device-${device})
    set_target_properties(
        board-${name}
        PROPERTIES
            DEVICE ${device}
            PACKAGE ${package}
    )
endfunction()
