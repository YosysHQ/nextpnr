# Run ./nextpnr-ice40 --json ice40/blinky.json --file python/dump_design.py
for cell in sorted(design.cells, key=lambda x: x.first):
    print("Cell {} : {}".format(cell.first, cell.second.type))
    for port in sorted(cell.second.ports, key=lambda x: x.first):
        dir = (" <-- ", " --> ", " <-> ")[int(port.second.type)]
        print("    {} {} {}".format(port.first, dir, port.second.net.name))
