# Run ./nextpnr-ice40 --json ice40/blinky.json --run python/dump_design.py
for cell in sorted(design.cells, key=lambda x: x.first):
    print("Cell {} : {}".format(cell.first, cell.second.type))
    print("\tPorts:")
    for port in sorted(cell.second.ports, key=lambda x: x.first):
        dir = (" <-- ", " --> ", " <-> ")[int(port.second.type)]
        if port.second.net is not None:
            print("\t\t{} {} {}".format(port.first, dir, port.second.net.name))

    if len(cell.second.attrs) > 0:
        print("\tAttrs:")
        for attr in cell.second.attrs:
            print("\t\t{}: {}".format(attr.first, attr.second))

    if len(cell.second.params) > 0:
        print("\tParams:")
        for param in cell.second.params:
            print("\t\t{}: {}".format(param.first, param.second))

    if not cell.second.bel.nil():
        print("\tBel: {}".format(chip.getBelName(cell.second.bel)))
    print()
