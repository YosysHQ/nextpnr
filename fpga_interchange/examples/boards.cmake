function(add_board)
    # ~~~
    # add_board(
    #    name <board name>
    #    device_family <device family>
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
    #   - device_family: the name of the family this device belongs to.
    #                    E.g. the xc7a35t device belongs to the xc7 family
    #   - device: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix
    #   - package: one of the packages available for a given device. E.g. cpg236
    #
    # Targets generated:
    #   - board-<name>

    set(options)
    set(oneValueArgs name device_family device package)
    set(multiValueArgs)

    cmake_parse_arguments(
        add_board
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(name ${add_board_name})
    set(device_family ${add_board_device_family})
    set(device ${add_board_device})
    set(package ${add_board_package})

    add_custom_target(board-${name} DEPENDS device-${device})
    set_target_properties(
        board-${name}
        PROPERTIES
            DEVICE_FAMILY ${device_family}
            DEVICE ${device}
            PACKAGE ${package}
    )
endfunction()
