# Run ./nextpnr-ice40 --json ice40/blinky.json --run python/dump_design.py
for cell, cinfo in sorted(ctx.cells, key=lambda x: x.first):
    print("Cell {} : {}".format(cell, cinfo.type))
    print("\tPorts:")
    for port, pinfo in sorted(cinfo.ports, key=lambda x: x.first):
        dir = (" <-- ", " --> ", " <-> ")[int(pinfo.type)]
        if pinfo.net is not None:
            print("\t\t{} {} {}".format(port, dir, pinfo.net.name))

    if len(cinfo.attrs) > 0:
        print("\tAttrs:")
        for attr, val in cinfo.attrs:
            print("\t\t{}: {}".format(attr, val))

    if len(cinfo.params) > 0:
        print("\tParams:")
        for param, val in cinfo.params:
            if val.isdigit():
                val = bin(int(val))[2:]
                val = "{}'b{}".format(len(val), val)
            print("\t\t{}: {}".format(param, val))

    if cinfo.bel is not None:
        print("\tBel: {}".format(cinfo.bel))
    print()
